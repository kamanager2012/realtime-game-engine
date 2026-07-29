#include "poker_engine/spectator/spectator_manager.h"

#include <algorithm>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::spectator {

// ==================== SpectatorEvent ====================

std::string SpectatorEvent::Serialize() const {
  nlohmann::json j;
  j["seq"] = sequence_id;
  j["tournament_id"] = tournament_id;
  j["hand_id"] = hand_id;
  j["type"] = static_cast<uint8_t>(type);
  j["timestamp"] = timestamp_ms;
  try {
    j["payload"] = nlohmann::json::parse(payload);
  } catch (...) {
    j["payload"] = payload;
  }
  return j.dump();
}

std::optional<SpectatorEvent> SpectatorEvent::Deserialize(const std::string& json_str) {
  try {
    auto j = nlohmann::json::parse(json_str);
    SpectatorEvent evt;
    evt.sequence_id = j.value("seq", 0ULL);
    evt.tournament_id = j.value("tournament_id", 0ULL);
    evt.hand_id = j.value("hand_id", 0ULL);
    evt.type = static_cast<SpectatorMessageType>(j.value("type", 0));
    evt.timestamp_ms = j.value("timestamp", 0.0);
    auto p = j.find("payload");
    if (p != j.end()) {
      evt.payload = p->is_string() ? p->get<std::string>() : p->dump();
    }
    return evt;
  } catch (const std::exception& e) {
    PE_LOG_ERROR("SpectatorEvent::Deserialize failed: {}", e.what());
    return std::nullopt;
  }
}

// ==================== EventBroadcaster ====================

EventBroadcaster::EventBroadcaster() = default;

void EventBroadcaster::RegisterSession(std::shared_ptr<SpectatorSession> session) {
  uint64_t tid = session->subscribed_tournament_id;
  std::unique_lock lock(global_mutex_);
  auto& viewers = tournament_viewers_[tid];
  std::unique_lock session_lock(viewers.mutex);
  viewers.sessions[session->session_id] = session;
  PE_LOG_INFO("Session {} registered for tournament {} (total: {})", session->session_id, tid,
              viewers.sessions.size());
}

void EventBroadcaster::UnregisterSession(int64_t session_id) {
  std::shared_lock lock(global_mutex_);
  for (auto& [tid, viewers] : tournament_viewers_) {
    std::unique_lock session_lock(viewers.mutex);
    viewers.sessions.erase(session_id);
  }
}

void EventBroadcaster::Broadcast(uint64_t tournament_id, const SpectatorEvent& event) {
  std::shared_lock lock(global_mutex_);
  auto it = tournament_viewers_.find(tournament_id);
  if (it == tournament_viewers_.end()) return;

  auto& viewers = it->second;
  std::shared_lock session_lock(viewers.mutex);

  std::string serialized;
  bool serialized_once = false;

  for (auto& [sid, session] : viewers.sessions) {
    if (!session->IsAlive()) continue;

    if (!serialized_once || session->is_video_mode) {
      serialized = event.Serialize();
      serialized_once = true;
    }

    {
      std::lock_guard<std::mutex> elock(session->event_mutex);
      session->pending_events.push(event);
    }
  }
}

void EventBroadcaster::Unicast(int64_t session_id, const SpectatorEvent& event) {
  std::shared_lock lock(global_mutex_);
  for (auto& [tid, viewers] : tournament_viewers_) {
    std::shared_lock session_lock(viewers.mutex);
    auto it = viewers.sessions.find(session_id);
    if (it != viewers.sessions.end()) {
      std::lock_guard<std::mutex> elock(it->second->event_mutex);
      it->second->pending_events.push(event);
      return;
    }
  }
}

int EventBroadcaster::SpectatorCount(uint64_t tournament_id) const {
  std::shared_lock lock(global_mutex_);
  auto it = tournament_viewers_.find(tournament_id);
  if (it == tournament_viewers_.end()) return 0;
  std::shared_lock session_lock(it->second.mutex);
  return static_cast<int>(it->second.sessions.size());
}

// ==================== SpectatorManager ====================

SpectatorManager::SpectatorManager(const SpectatorConfig& config) : config_(config) {}

SpectatorManager::~SpectatorManager() { Stop(); }

bool SpectatorManager::Start() {
  running_ = true;
  broadcast_thread_ = std::thread(&SpectatorManager::BroadcastLoop, this);
  PE_LOG_INFO("Spectator manager started on port {}", config_.spectator_port);
  return true;
}

void SpectatorManager::Stop() {
  running_ = false;
  queue_cv_.notify_all();
  if (broadcast_thread_.joinable()) {
    broadcast_thread_.join();
  }
  CleanupExpiredSessions();
  PE_LOG_INFO("Spectator manager stopped");
}

bool SpectatorManager::Subscribe(int64_t session_id, uint64_t tournament_id) {
  std::unique_lock lock(sessions_mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) return false;

  it->second->subscribed_tournament_id = tournament_id;
  broadcaster_.RegisterSession(it->second);

  // 发送当前锦标赛状态
  SpectatorEvent init_evt;
  init_evt.type = SpectatorMessageType::TournamentState;
  init_evt.payload = CreateTournamentStateEvent(tournament_id).payload;
  broadcaster_.Unicast(session_id, init_evt);

  PE_LOG_INFO("Session {} subscribed to tournament {}", session_id, tournament_id);
  return true;
}

bool SpectatorManager::Unsubscribe(int64_t session_id) {
  std::unique_lock lock(sessions_mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) return false;

  broadcaster_.UnregisterSession(session_id);
  it->second->subscribed_tournament_id = 0;
  return true;
}

void SpectatorManager::HandleChat(int64_t session_id, const std::string& message) {
  std::shared_lock lock(sessions_mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) return;

  SpectatorChatMessage chat;
  chat.player_id = it->second->player_id;
  chat.display_name = it->second->display_name;
  chat.message = message;
  chat.timestamp = std::chrono::system_clock::now();
  chat.is_spectator = true;

  // 广播聊天消息
  SpectatorEvent evt;
  evt.type = SpectatorMessageType::ChatMessage;

  nlohmann::json payload;
  payload["player_name"] = chat.display_name;
  payload["message"] = chat.message;
  payload["is_spectator"] = chat.is_spectator;
  evt.payload = payload.dump();

  // 找出订阅了相同锦标赛的会话
  auto sub_tid = it->second->subscribed_tournament_id;
  if (sub_tid > 0) {
    broadcaster_.Broadcast(sub_tid, evt);
  }
}

TournamentSnapshot SpectatorManager::GetSnapshot(uint64_t tournament_id) const {
  TournamentSnapshot snap;
  snap.tournament_id = tournament_id;
  // 实际实现中从 TournamentManager 获取状态
  return snap;
}

void SpectatorManager::OnTournamentEvent(uint64_t tournament_id,
                                         const tournament::TournamentEvent& event) {
  SpectatorEvent sev;
  sev.tournament_id = tournament_id;
  sev.hand_id = 0;

  switch (event.type) {
    case tournament::TournamentEvent::Type::PlayerRegistered:
      sev.type = SpectatorMessageType::PlayerJoin;
      sev.payload = nlohmann::json{{"player_name", event.detail}}.dump();
      break;
    case tournament::TournamentEvent::Type::PlayerEliminated:
      sev.type = SpectatorMessageType::PlayerLeave;
      sev.payload = nlohmann::json{{"player_name", event.detail}}.dump();
      break;
    case tournament::TournamentEvent::Type::BlindLevelUp:
      sev.type = SpectatorMessageType::TournamentState;
      sev.payload = nlohmann::json{{"blind_change", event.detail}}.dump();
      break;
    case tournament::TournamentEvent::Type::HandCompleted:
      sev.type = SpectatorMessageType::TournamentState;
      sev.payload = nlohmann::json{{"event", "hand_complete"}, {"player", event.detail}}.dump();
      break;
    case tournament::TournamentEvent::Type::TournamentEnd:
      sev.type = SpectatorMessageType::TournamentResult;
      sev.payload = nlohmann::json{{"winner", event.detail}}.dump();
      break;
    default:
      return;
  }

  broadcaster_.Broadcast(tournament_id, sev);
}

void SpectatorManager::OnHandEvent(uint64_t tournament_id, uint64_t hand_id,
                                   const game::GameAction& action) {
  SpectatorEvent evt;
  evt.type = SpectatorMessageType::HandEvent;
  evt.tournament_id = tournament_id;
  evt.hand_id = hand_id;

  nlohmann::json payload;
  payload["action"] = action.type == game::ActionType::FOLD     ? "fold"
                      : action.type == game::ActionType::CHECK  ? "check"
                      : action.type == game::ActionType::CALL   ? "call"
                      : action.type == game::ActionType::BET    ? "bet"
                      : action.type == game::ActionType::RAISE  ? "raise"
                      : action.type == game::ActionType::ALL_IN ? "all_in"
                                                                : "other";
  payload["amount"] = action.amount;
  payload["player_id"] = action.player_id;
  payload["round"] = static_cast<int>(action.street);
  evt.payload = payload.dump();

  broadcaster_.Broadcast(tournament_id, evt);
}

void SpectatorManager::OnHandComplete(uint64_t tournament_id, uint64_t hand_id,
                                      const game::GameState& final_state) {
  SpectatorEvent evt;
  evt.type = SpectatorMessageType::HandState;
  evt.tournament_id = tournament_id;
  evt.hand_id = hand_id;

  nlohmann::json payload;
  payload["phase"] = static_cast<int>(final_state.GetPhase());
  payload["pot"] = final_state.GetPot();

  // Community cards
  const auto& comm = final_state.GetCommunity();
  nlohmann::json cc = nlohmann::json::array();
  for (uint8_t i = 0; i < comm.count; ++i) cc.push_back(comm.cards[i]);
  payload["community_cards"] = cc;

  // Player final states
  nlohmann::json players_json = nlohmann::json::array();
  for (const auto& p : final_state.AllPlayers()) {
    // Show all seated players with cards, including folded
    if (p.seat_state == game::SeatState::EMPTY) continue;
    nlohmann::json pp;
    pp["player_id"] = p.id;
    pp["seat_index"] = p.seat;
    pp["chips"] = p.chips;
    pp["status"] = p.IsFolded()   ? "folded"
                   : p.IsAllIn()  ? "all_in"
                   : p.IsActive() ? "active"
                                  : "sitting_out";
    pp["total_invested"] = p.bet_info.total_invested;
    pp["display_name"] = p.name;
    players_json.push_back(pp);
  }
  payload["players"] = players_json;

  evt.payload = payload.dump();
  broadcaster_.Broadcast(tournament_id, evt);

  auto leader_evt = CreateLeaderboardEvent(tournament_id);
  broadcaster_.Broadcast(tournament_id, leader_evt);
}

void SpectatorManager::BroadcastLoop() {
  using namespace std::chrono;

  auto last_broadcast = steady_clock::now();

  while (running_) {
    // 定期广播锦标赛状态
    auto now = steady_clock::now();
    auto elapsed = duration_cast<milliseconds>(now - last_broadcast).count();

    if (elapsed >= config_.state_broadcast_interval_ms) {
      // 处理事件队列
      bool has_event = false;
      PendingBroadcast pb;
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!broadcast_queue_.empty()) {
          pb = std::move(broadcast_queue_.front());
          broadcast_queue_.pop();
          has_event = true;
        }
      }

      if (has_event) {
        auto send_start = steady_clock::now();
        broadcaster_.Broadcast(pb.tournament_id, pb.event);
        auto send_elapsed = duration_cast<microseconds>(steady_clock::now() - send_start).count();

        total_events_broadcast_++;
        total_broadcast_latency_us_ += send_elapsed;
      }

      last_broadcast = now;
    }

    // 定期清理过期会话
    if (++cleanup_counter_ % 100 == 0) {
      CleanupExpiredSessions();
    }

    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait_for(lock, milliseconds(10),
                       [this]() { return !broadcast_queue_.empty() || !running_; });
  }
}

void SpectatorManager::CleanupExpiredSessions() {
  std::vector<int64_t> expired;
  {
    std::unique_lock lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
      if (!session->IsAlive()) {
        expired.push_back(id);
      }
    }
  }

  for (auto id : expired) {
    PE_LOG_INFO("Cleaning up expired spectator session {}", id);
    broadcaster_.UnregisterSession(id);
    {
      std::unique_lock lock(sessions_mutex_);
      sessions_.erase(id);
    }
  }
}

SpectatorEvent SpectatorManager::CreateTournamentStateEvent(uint64_t tournament_id) const {
  SpectatorEvent evt;
  evt.type = SpectatorMessageType::TournamentState;
  evt.tournament_id = tournament_id;

  TournamentSnapshot snap = GetSnapshot(tournament_id);

  nlohmann::json payload;
  payload["tournament_id"] = snap.tournament_id;
  payload["name"] = snap.tournament_name;
  payload["status"] = snap.status;
  payload["current_blind_level"] = snap.current_blind_level;
  payload["small_blind"] = snap.small_blind;
  payload["big_blind"] = snap.big_blind;
  payload["ante"] = snap.ante;
  payload["remaining_players"] = snap.remaining_players;
  payload["total_players"] = snap.total_players;
  payload["prize_pool"] = snap.prize_pool;
  payload["elapsed_seconds"] = snap.elapsed_seconds;

  if (snap.current_hand) {
    nlohmann::json hand_json;
    hand_json["hand_id"] = snap.current_hand->hand_id;
    hand_json["phase"] = snap.current_hand->phase;
    hand_json["pot"] = snap.current_hand->pot;
    hand_json["current_bet"] = snap.current_hand->current_bet;
    hand_json["dealer_seat"] = snap.current_hand->dealer_seat;
    hand_json["acting_player_id"] = snap.current_hand->acting_player_id;
    payload["current_hand"] = hand_json;
  }

  nlohmann::json players_json = nlohmann::json::array();
  for (const auto& p : snap.players) {
    nlohmann::json pp;
    pp["player_id"] = p.player_id;
    pp["display_name"] = p.display_name;
    pp["chips"] = p.chips;
    pp["seat_index"] = p.seat_index;
    pp["status"] = p.status;
    pp["total_won"] = p.total_won;
    pp["is_ai"] = p.is_ai;
    pp["is_active"] = p.is_active;
    pp["place"] = p.place;
    players_json.push_back(pp);
  }
  payload["players"] = players_json;

  nlohmann::json lb_json = nlohmann::json::array();
  for (const auto& p : snap.leaderboard) {
    lb_json.push_back({{"player_id", p.player_id},
                       {"display_name", p.display_name},
                       {"chips", p.chips},
                       {"total_won", p.total_won},
                       {"place", p.place}});
  }
  payload["leaderboard"] = lb_json;

  evt.payload = payload.dump();
  return evt;
}

SpectatorEvent SpectatorManager::CreateLeaderboardEvent(uint64_t tournament_id) const {
  SpectatorEvent evt;
  evt.type = SpectatorMessageType::LeaderboardUpdate;
  evt.tournament_id = tournament_id;

  TournamentSnapshot snap = GetSnapshot(tournament_id);
  nlohmann::json lb_json = nlohmann::json::array();
  for (const auto& p : snap.leaderboard) {
    lb_json.push_back({{"player_id", p.player_id},
                       {"display_name", p.display_name},
                       {"chips", p.chips},
                       {"total_won", p.total_won},
                       {"is_ai", p.is_ai}});
  }
  evt.payload = lb_json.dump();
  return evt;
}

std::vector<SpectatorEvent> SpectatorManager::GetHandHistory(
    [[maybe_unused]] uint64_t tournament_id, [[maybe_unused]] uint64_t hand_id,
    [[maybe_unused]] int limit) const {
  // 实际实现中查询 action_log 表
  std::vector<SpectatorEvent> events;
  return events;
}

typename SpectatorManager::Stats SpectatorManager::GetStats() const {
  Stats s;
  s.total_sessions = static_cast<int>(sessions_.size());
  s.active_sessions = 0;
  for (auto& [id, session] : sessions_) {
    if (session->IsAlive()) s.active_sessions++;
  }
  s.events_broadcast = static_cast<int>(total_events_broadcast_.load());
  int64_t total_us = total_broadcast_latency_us_.load();
  s.avg_broadcast_latency_ms =
      s.events_broadcast > 0 ? (total_us / s.events_broadcast) / 1000.0 : 0.0;
  return s;
}

}  // namespace poker_engine::spectator
