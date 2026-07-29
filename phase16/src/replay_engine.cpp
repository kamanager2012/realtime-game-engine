#include "poker_engine/replay/replay_engine.h"

#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

#include "poker_engine/base/logging.h"

namespace poker_engine::replay {

using poker_engine::game::GamePhase;

ReplayEngine::ReplayEngine(persistence::DatabaseManager& db) : db_(db) {}

ReplayEngine::~ReplayEngine() { Stop(); }

std::vector<int64_t> ReplayEngine::ListHands(const ReplayQuery& query) {
  std::vector<int64_t> result;
  std::string sql =
      "SELECT DISTINCT h.id FROM hand_histories h JOIN hand_players hp ON h.id = hp.hand_id " +
      query.ToSQLWhere() + " ORDER BY h.id DESC LIMIT " + std::to_string(query.limit) + " OFFSET " +
      std::to_string(query.offset);
  db_.Query(sql, [&](const std::vector<std::string>& row) {
    result.push_back(std::stoll(row[0]));
    return true;
  });
  return result;
}

std::optional<HandSnapshot> ReplayEngine::GetHandSummary(int64_t hand_id) {
  auto hand = hand_repo_->GetHand(hand_id);
  if (!hand.has_value()) return std::nullopt;
  HandSnapshot snap;
  snap.hand_id = hand->hand_id;
  snap.hand_number = hand->hand_number;
  snap.phase = hand->phase;
  snap.total_pot = hand->pot_amount;
  try {
    auto j = nlohmann::json::parse(hand->winners_json);
    for (auto& w : j) snap.winners.push_back(w.get<int64_t>());
  } catch (const std::exception& e) {
    PE_LOG_WARN("ReplayEngine: failed to parse winners_json for hand {}: {}", hand->hand_id,
                e.what());
  }
  try {
    auto j = nlohmann::json::parse(hand->community_cards);
    for (auto& c : j) snap.community_cards.push_back(c.get<uint8_t>());
  } catch (const std::exception& e) {
    PE_LOG_WARN("ReplayEngine: failed to parse community_cards for hand {}: {}", hand->hand_id,
                e.what());
  }
  return snap;
}

bool ReplayEngine::StartReplay(int64_t hand_id, ReplayObserver* observer,
                               const ReplayConfig& config) {
  if (is_playing_) Stop();
  if (!LoadHandData(hand_id)) {
    observer->OnError("Failed to load hand data: " + std::to_string(hand_id));
    return false;
  }
  observer_ = observer;
  config_ = config;
  current_hand_id_ = hand_id;
  current_event_index_ = 0;
  current_time_ = 0.0;
  is_paused_ = false;
  stop_requested_ = false;
  is_playing_ = true;
  BuildEventTimeline();
  replay_thread_ = std::thread(&ReplayEngine::ReplayLoop, this);
  PE_LOG_INFO("Replay started: hand_id={}, events={}", hand_id, event_timeline_.size());
  return true;
}

void ReplayEngine::Pause() { is_paused_ = true; }
void ReplayEngine::Resume() { is_paused_ = false; }

void ReplayEngine::Seek(double seconds) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  current_time_ = seconds;
  for (size_t i = 0; i < event_timeline_.size(); ++i) {
    if (event_timeline_[i].timestamp >= seconds) {
      current_event_index_ = i;
      break;
    }
  }
  // Rebuild from scratch up to the seek position
  snapshot_.players.clear();
  community_cards_.clear();
  player_investments_.clear();
  player_chips_.clear();
  current_phase_ = GamePhase::PREFLOP_BETTING;
  current_bet_ = 0;
  for (size_t i = 0; i < event_timeline_.size(); ++i) {
    if (event_timeline_[i].timestamp >= seconds) break;
    ProcessEvent(event_timeline_[i]);
  }
}

void ReplayEngine::Stop() {
  stop_requested_ = true;
  is_playing_ = false;
  if (replay_thread_.joinable()) replay_thread_.join();
  PE_LOG_INFO("Replay stopped");
}

bool ReplayEngine::LoadHandData(int64_t hand_id) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  auto hand = hand_repo_->GetHand(hand_id);
  if (!hand.has_value()) return false;
  snapshot_ = HandSnapshot();
  snapshot_.hand_id = hand->hand_id;
  snapshot_.hand_number = hand->hand_number;
  snapshot_.total_pot = hand->pot_amount;
  snapshot_.phase = hand->phase;
  try {
    auto j = nlohmann::json::parse(hand->winners_json);
    for (auto& w : j) snapshot_.winners.push_back(w.get<int64_t>());
  } catch (const std::exception& e) {
    PE_LOG_WARN("ReplayEngine: failed to parse winners_json for hand {}: {}", hand->hand_id,
                e.what());
  }
  try {
    auto j = nlohmann::json::parse(hand->community_cards);
    for (auto& c : j) {
      snapshot_.community_cards.push_back(c.get<uint8_t>());
      community_cards_.push_back(c.get<uint8_t>());
    }
  } catch (const std::exception& e) {
    PE_LOG_WARN("ReplayEngine: failed to parse community_cards for hand {}: {}", hand->hand_id,
                e.what());
  }
  return true;
}

void ReplayEngine::BuildEventTimeline() {
  event_timeline_.clear();
  player_investments_.clear();
  player_chips_.clear();
  current_phase_ = GamePhase::PREFLOP_BETTING;
  current_bet_ = 0;
  std::string sql =
      "SELECT id, player_id, round_name, action_type, amount, timestamp FROM action_log WHERE "
      "hand_id = " +
      std::to_string(current_hand_id_) + " ORDER BY id ASC";
  db_.Query(sql, [&](const std::vector<std::string>& row) {
    ReplayEvent evt;
    evt.sequence_id = std::stoll(row[0]);
    evt.hand_id = current_hand_id_;
    evt.player_id = std::stoll(row[1]);
    evt.timestamp = 0.0;
    evt.type = ReplayEventType::ActionTaken;
    nlohmann::json details;
    details["round"] = row[2];
    details["action"] = row[3];
    details["amount"] = std::stoll(row[4]);
    evt.details = details.dump();
    event_timeline_.push_back(evt);
    return true;
  });
  std::sort(
      event_timeline_.begin(), event_timeline_.end(),
      [](const ReplayEvent& a, const ReplayEvent& b) { return a.sequence_id < b.sequence_id; });
  for (size_t i = 0; i < event_timeline_.size(); ++i) {
    event_timeline_[i].timestamp = i * config_.event_delay_ms / 1000.0;
  }
  PE_LOG_INFO("Built timeline: {} events for hand #{}", event_timeline_.size(), current_hand_id_);
}

void ReplayEngine::ReplayLoop() {
  for (size_t i = current_event_index_; i < event_timeline_.size() && !stop_requested_; ++i) {
    while (is_paused_ && !stop_requested_)
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (stop_requested_) break;
    auto& evt = event_timeline_[i];
    current_time_ = evt.timestamp;
    current_event_index_ = i + 1;
    ProcessEvent(evt);
    PushSnapshot();
    double delay = config_.event_delay_ms / config_.playback_speed;
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delay)));
  }
  is_playing_ = false;
  if (observer_) observer_->OnComplete(current_hand_id_);
}

void ReplayEngine::ProcessEvent(const ReplayEvent& event) {
  try {
    nlohmann::json details = nlohmann::json::parse(event.details);
    switch (event.type) {
      case ReplayEventType::ActionTaken: {
        std::string action = details.value("action", "");
        int64_t amount = details.value("amount", 0);
        int32_t pid = event.player_id;
        player_investments_[pid] += amount;
        if (action == "call" || action == "bet" || action == "raise" || action == "all_in") {
          current_bet_ = std::max(current_bet_, player_investments_[pid]);
        }
        for (auto& p : snapshot_.players) {
          if (p.player_id == pid) {
            p.actions.push_back(action + " " + std::to_string(amount));
            p.chips_at_end -= amount;
            break;
          }
        }
        std::string round = details.value("round", "");
        if (round == "flop" || round == "0")
          current_phase_ = GamePhase::FLOP_BETTING;
        else if (round == "turn" || round == "1")
          current_phase_ = GamePhase::TURN_BETTING;
        else if (round == "river" || round == "2")
          current_phase_ = GamePhase::RIVER_BETTING;
        else if (round == "showdown" || round == "completed")
          current_phase_ = GamePhase::SHOWDOWN;
        break;
      }
      case ReplayEventType::DealCommunity: {
        if (details.contains("cards")) {
          auto new_cards = details["cards"].get<std::vector<uint8_t>>();
          community_cards_.insert(community_cards_.end(), new_cards.begin(), new_cards.end());
        }
        break;
      }
      case ReplayEventType::HandStart: {
        current_phase_ = GamePhase::PREFLOP_BETTING;
        current_bet_ = 0;
        break;
      }
      case ReplayEventType::HandEnd: {
        current_phase_ = GamePhase::HAND_COMPLETE;
        break;
      }
      default:
        break;
    }
  } catch (const std::exception& e) {
    PE_LOG_WARN("ReplayEngine: error processing event: {}", e.what());
  }
}

void ReplayEngine::RebuildState(const ReplayEvent& base_event) {
  snapshot_.players.clear();
  community_cards_.clear();
  player_investments_.clear();
  current_phase_ = GamePhase::PREFLOP_BETTING;
  current_bet_ = 0;
  for (size_t i = 0; i < event_timeline_.size(); ++i) {
    if (event_timeline_[i].timestamp >= base_event.timestamp) break;
    ProcessEvent(event_timeline_[i]);
  }
}

void ReplayEngine::PushSnapshot() {
  if (!observer_) return;
  snapshot_.total_pot = 0;
  for (auto& [pid, inv] : player_investments_) snapshot_.total_pot += inv;
  snapshot_.players.clear();
  for (auto& [pid, inv] : player_investments_) {
    HandSnapshot::PlayerSnap ps;
    ps.player_id = pid;
    ps.chips_at_start = player_chips_.count(pid) ? player_chips_[pid] + inv : 1500;
    ps.chips_at_end = ps.chips_at_start - inv;
    ps.net_profit = 0;
    ps.status = "active";
    snapshot_.players.push_back(ps);
  }
  snapshot_.community_cards = community_cards_;
  observer_->OnSnapshot(snapshot_);
}

}  // namespace poker_engine::replay
