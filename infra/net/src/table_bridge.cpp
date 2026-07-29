#include "poker_engine/network/table_bridge.h"

#include <sstream>

#include "poker_engine/base/logging.h"
#include "poker_engine/network/ws_server.h"

namespace poker_engine::network {

namespace {

const char* PhaseNameForClient(game::GamePhase phase) {
  using game::GamePhase;
  switch (phase) {
    case GamePhase::WAITING:
      return "waiting";
    case GamePhase::DEALING:
    case GamePhase::PREFLOP_BETTING:
      return "preflop";
    case GamePhase::FLOP_DEALING:
    case GamePhase::FLOP_BETTING:
      return "flop";
    case GamePhase::TURN_DEALING:
    case GamePhase::TURN_BETTING:
      return "turn";
    case GamePhase::RIVER_DEALING:
    case GamePhase::RIVER_BETTING:
      return "river";
    case GamePhase::SHOWDOWN:
    case GamePhase::PAYOUT:
      return "showdown";
    case GamePhase::HAND_COMPLETE:
      return "handOver";
    default:
      return "waiting";
  }
}

const char* PlayerStatusForClient(game::SeatState state) {
  using game::SeatState;
  switch (state) {
    case SeatState::FOLDED:
      return "folded";
    case SeatState::ALL_IN:
      return "all_in";
    case SeatState::SITTING_OUT:
      return "sitting_out";
    default:
      return "active";
  }
}

game::ActionType ActionFromString(const std::string& action) {
  using game::ActionType;
  if (action == "fold") return ActionType::FOLD;
  if (action == "check") return ActionType::CHECK;
  if (action == "call") return ActionType::CALL;
  if (action == "bet") return ActionType::BET;
  if (action == "raise") return ActionType::RAISE;
  if (action == "all_in" || action == "allin") return ActionType::ALL_IN;
  return ActionType::CHECK;
}

std::string JsonEscape(const std::string& value) {
  std::string out;
  for (char c : value) {
    if (c == '"' || c == '\\') out.push_back('\\');
    if (static_cast<unsigned char>(c) < 0x20) continue;
    out.push_back(c);
  }
  return out;
}

}  // namespace

// ==================== TableBridge ====================

TableBridge::TableBridge(int64_t table_id, std::unique_ptr<poker_engine::game::Table> table,
                         WSServer* ws_server)
    : table_id_(table_id), table_(std::move(table)), ws_server_(ws_server) {
  // Wire up Table event callback → network broadcast
  table_->SetCallback([this](const game::TableEvent& event) { OnTableEvent(event); });
}

// ========== 网络 → 游戏逻辑 ==========

void TableBridge::HandleAuth(WSSession::Ptr session, const nlohmann::json& payload) {
  std::string token = payload.value("token", "");
  if (token.empty()) {
    nlohmann::json resp;
    resp["op"] = protocol::OP_AUTH_RESULT;
    resp["data"]["success"] = false;
    resp["data"]["error"] = "Empty token";
    SendTo(session, resp);
    return;
  }

  int64_t player_id = 0;
  try {
    player_id = std::stoll(token);
  } catch (...) {
    nlohmann::json resp;
    resp["op"] = protocol::OP_AUTH_RESULT;
    resp["data"]["success"] = false;
    resp["data"]["error"] = "Invalid token format";
    SendTo(session, resp);
    return;
  }

  if (player_id <= 0) {
    nlohmann::json resp;
    resp["op"] = protocol::OP_AUTH_RESULT;
    resp["data"]["success"] = false;
    resp["data"]["error"] = "Invalid player ID";
    SendTo(session, resp);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(player_session_mutex_);
    player_session_map_[player_id] = session->session_id();
  }
  {
    std::lock_guard<std::mutex> lock(session_player_mutex_);
    session_player_map_[session->session_id()] = player_id;
  }

  PE_LOG_INFO("Table {}: Player {} authenticated, session={}", table_id_, player_id,
              session->session_id());

  nlohmann::json resp;
  resp["op"] = protocol::OP_AUTH_RESULT;
  resp["data"]["success"] = true;
  resp["data"]["player_id"] = player_id;
  resp["data"]["table_id"] = table_id_;
  SendTo(session, resp);

  SendFullState(session);
}

void TableBridge::HandleJoinTable(WSSession::Ptr session, const nlohmann::json& payload) {
  if (!session->authenticated()) {
    SendError(session, 401, "Not authenticated");
    return;
  }

  int64_t player_id = session->player_id();
  if (session->table_id() >= 0) {
    SendError(session, 400, "Already at a table");
    return;
  }

  int64_t buy_in = payload.value("buy_in", 10000LL);
  std::string name = payload.value("name", "Player" + std::to_string(player_id));
  int seat = payload.value("seat", -1);

  bool ok;
  if (seat >= 0 && seat <= 255) {
    ok = table_->JoinTable(static_cast<int32_t>(player_id), name, static_cast<double>(buy_in),
                           static_cast<uint8_t>(seat));
  } else {
    ok = table_->JoinTable(static_cast<int32_t>(player_id), name, static_cast<double>(buy_in));
  }

  if (!ok) {
    SendError(session, 400, "Failed to join table (full or invalid)");
    return;
  }

  session->set_table_id(table_id_);

  PE_LOG_INFO("Table {}: Player {} joined (buy-in={}), session={}", table_id_, player_id, buy_in,
              session->session_id());

  nlohmann::json resp;
  resp["op"] = protocol::OP_JOIN_TABLE;
  resp["data"]["success"] = true;
  resp["data"]["player_id"] = player_id;
  resp["data"]["table_id"] = table_id_;
  SendTo(session, resp);

  // Broadcast join to others
  nlohmann::json notify;
  notify["op"] = protocol::OP_PLAYER_JOINED;
  notify["data"]["player_id"] = player_id;
  notify["data"]["display_name"] = name;
  BroadcastExcept(session->session_id(), notify);
}

void TableBridge::HandleLeaveTable(WSSession::Ptr session) {
  if (!session->authenticated() || session->table_id() < 0) return;

  int64_t player_id = session->player_id();
  table_->LeaveTable(static_cast<int32_t>(player_id));
  session->set_table_id(-1);

  nlohmann::json resp;
  resp["op"] = protocol::OP_PLAYER_LEFT;
  resp["data"]["player_id"] = player_id;
  Broadcast(resp);

  PE_LOG_INFO("Table {}: Player {} left", table_id_, player_id);
}

void TableBridge::HandlePlayerAction(WSSession::Ptr session, const nlohmann::json& payload) {
  if (!session->authenticated() || session->table_id() < 0) return;

  int64_t player_id = session->player_id();
  std::string action_str = payload.value("action", "");
  int64_t amount = payload.value("amount", 0LL);

  game::GameAction ga;
  ga.type = ActionFromString(action_str);
  ga.amount = static_cast<double>(amount);
  ga.player_id = static_cast<int32_t>(player_id);

  bool ok = table_->PlayerAction(static_cast<int32_t>(player_id), ga);

  nlohmann::json resp;
  resp["op"] = protocol::OP_ACTION_RESULT;
  resp["data"]["player_id"] = player_id;
  resp["data"]["action"] = action_str;
  resp["data"]["valid"] = ok;
  if (!ok) resp["data"]["error"] = "Invalid action";
  SendTo(session, resp);

  PE_LOG_INFO("Table {}: Player {} {} {} (valid={})", table_id_, player_id, action_str, amount, ok);
}

void TableBridge::HandleChat(WSSession::Ptr session, const nlohmann::json& payload) {
  if (!session->authenticated()) return;

  int64_t player_id = session->player_id();
  std::string message = payload.value("message", "");
  if (message.empty() || message.size() > 500) return;

  nlohmann::json msg;
  msg["op"] = protocol::OP_CHAT_MESSAGE;
  msg["data"]["player_id"] = player_id;
  msg["data"]["message"] = message;
  Broadcast(msg);
}

void TableBridge::HandlePing(WSSession::Ptr session) {
  nlohmann::json pong;
  pong["op"] = protocol::OP_PONG;
  pong["ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
  SendTo(session, pong);
}

void TableBridge::HandleStartHand() { table_->StartHand(); }

// ========== Bot ==========

int TableBridge::AddBots(int count, int64_t buy_in) {
  int added = 0;
  for (int i = 0; i < count; ++i) {
    const int bot_id = next_bot_id_--;
    const std::string name = "Bot" + std::to_string(-bot_id);
    if (!table_->JoinTable(bot_id, name, static_cast<double>(buy_in))) continue;
    ++added;
  }
  return added;
}

void TableBridge::ProcessBotActions() {
  for (int i = 0; i < 30; ++i) {
    const auto& state = table_->GetGameState();
    int32_t current = state.GetCurrentPlayerId();
    if (current >= 0) break;

    game::GameAction ga;
    ga.player_id = current;
    double current_player_bet = 0;
    for (const auto& p : state.AllPlayers()) {
      if (p.id == current) {
        current_player_bet = p.bet_info.current_bet;
        break;
      }
    }
    ga.type = (state.GetCurrentBet() - current_player_bet > 0.001) ? game::ActionType::CALL
                                                                   : game::ActionType::CHECK;
    if (!table_->PlayerAction(current, ga)) break;
  }
}

int64_t TableBridge::min_buy_in() const {
  return static_cast<int64_t>(
      table_->GetGameState().AllPlayers().empty() ? 1000 : 0);  // 0 = no min in this context
}

// ========== Table event callback ==========

void TableBridge::OnTableEvent(const game::TableEvent& event) {
  using game::TableEvent;
  switch (event.type) {
    case TableEvent::HAND_STARTED: {
      nlohmann::json msg;
      msg["op"] = protocol::OP_HAND_START;
      msg["data"]["message"] = event.message;
      Broadcast(msg);
      break;
    }
    case TableEvent::HAND_ENDED:
    case TableEvent::PAYOUT: {
      nlohmann::json msg;
      msg["op"] = protocol::OP_HAND_END;
      msg["data"]["message"] = event.message;
      Broadcast(msg);
      break;
    }
    case TableEvent::ACTION: {
      nlohmann::json msg;
      msg["op"] = protocol::OP_ACTION_RESULT;
      msg["data"]["message"] = event.message;
      Broadcast(msg);
      break;
    }
    case TableEvent::SHOWDOWN: {
      nlohmann::json msg;
      msg["op"] = protocol::OP_HAND_END;
      msg["data"]["type"] = "showdown";
      msg["data"]["message"] = event.message;
      Broadcast(msg);
      break;
    }
    default:
      break;
  }

  // Always broadcast full state after any event
  auto state = BuildTableState();
  Broadcast(state);
}

// ========== State serialization ==========

nlohmann::json TableBridge::BuildTableState(int64_t viewer_player_id) const {
  const auto& state = table_->GetGameState();

  nlohmann::json msg;
  msg["op"] = protocol::OP_TABLE_STATE;
  auto& data = msg["data"];

  data["table_id"] = table_id_;
  data["phase"] = PhaseNameForClient(state.GetPhase());
  data["status"] = state.IsHandInProgress() ? "playing" : "idle";
  data["pot"] = static_cast<int64_t>(state.GetPot());
  data["current_bet"] = static_cast<int64_t>(state.GetCurrentBet());
  data["current_player_id"] = state.GetCurrentPlayerId();

  // Community cards
  const auto& comm = state.GetCommunity();
  auto cc = nlohmann::json::array();
  for (uint8_t i = 0; i < comm.count; ++i) {
    cc.push_back(static_cast<int>(comm.cards[i]));
  }
  data["community_cards"] = cc;

  // Players
  auto players = nlohmann::json::array();
  for (const auto& p : state.AllPlayers()) {
    if (p.seat_state == game::SeatState::EMPTY) continue;
    nlohmann::json pj;
    pj["player_id"] = p.id;
    pj["seat_index"] = static_cast<int>(p.seat);
    pj["chips"] = static_cast<int64_t>(p.chips);
    pj["bet_this_round"] = static_cast<int64_t>(p.bet_info.current_bet);
    pj["total_invested"] = static_cast<int64_t>(p.bet_info.total_invested);
    pj["status"] = PlayerStatusForClient(p.seat_state);
    pj["display_name"] = p.name;

    // Hole cards: only show to the viewer themselves
    if (p.hole_cards.IsDealt() && p.id == static_cast<int32_t>(viewer_player_id)) {
      pj["hole_cards"] = {static_cast<int>(p.hole_cards.card1()),
                          static_cast<int>(p.hole_cards.card2())};
    } else {
      pj["hole_cards"] = nlohmann::json::array();
    }

    players.push_back(pj);
  }
  data["players"] = players;

  return msg;
}

void TableBridge::SendFullState(WSSession::Ptr session) {
  int64_t viewer_id = 0;
  {
    std::lock_guard<std::mutex> lock(session_player_mutex_);
    auto it = session_player_map_.find(session->session_id());
    if (it != session_player_map_.end()) viewer_id = it->second;
  }
  auto state = BuildTableState(viewer_id);
  SendTo(session, state);
}

// ========== Send helpers ==========

void TableBridge::SendTo(WSSession::Ptr session, const nlohmann::json& msg) { session->Send(msg); }

void TableBridge::SendTo(int64_t session_id, const nlohmann::json& msg) {
  ws_server_->SendTo(session_id, msg);
}

void TableBridge::Broadcast(const nlohmann::json& msg) {
  ws_server_->BroadcastToTable(table_id_, msg);
}

void TableBridge::BroadcastExcept(int64_t exclude_session_id, const nlohmann::json& msg) {
  std::lock_guard<std::mutex> lock(player_session_mutex_);
  for (auto& [pid, sid] : player_session_map_) {
    if (sid != exclude_session_id) {
      ws_server_->SendTo(sid, msg);
    }
  }
}

void TableBridge::SendError(WSSession::Ptr session, int code, const std::string& message) {
  nlohmann::json msg;
  msg["op"] = protocol::OP_ERROR;
  msg["data"]["code"] = code;
  msg["data"]["message"] = message;
  session->Send(msg);
}

int64_t TableBridge::FindSessionForPlayer(int64_t player_id) const {
  std::lock_guard<std::mutex> lock(player_session_mutex_);
  auto it = player_session_map_.find(player_id);
  return it != player_session_map_.end() ? it->second : -1;
}

// ==================== SimpleAuthService ====================

void SimpleAuthService::AddToken(const std::string& token, int64_t player_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  token_map_[token] = player_id;
}

std::optional<int64_t> SimpleAuthService::VerifyToken(const std::string& token) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = token_map_.find(token);
  if (it != token_map_.end()) return it->second;
  return std::nullopt;
}

void SimpleAuthService::RemoveToken(const std::string& token) {
  std::lock_guard<std::mutex> lock(mutex_);
  token_map_.erase(token);
}

}  // namespace poker_engine::network
