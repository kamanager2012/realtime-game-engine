#include "poker_engine/network/table_actor.h"

#include <nlohmann/json.hpp>

#include "poker_engine/base/logging.h"
#include "poker_engine/network/session_manager.h"

namespace poker_engine::network {

using json = nlohmann::json;

TableActor::TableActor(const std::string& table_id)
    : Actor(std::hash<std::string>{}(table_id)), sessions_(SessionManager::Instance()) {
  state_.table_id = table_id;
  memset(&state_.game_state, 0, sizeof(state_.game_state));
  state_.game_state.table_id = table_id;
}

void TableActor::OnReceive(const concurrency::MessageEnvelope& msg) {
  if (msg.message_type == "join_table")
    HandleJoinTable(msg);
  else if (msg.message_type == "leave_table")
    HandleLeaveTable(msg);
  else if (msg.message_type == "player_action")
    HandlePlayerAction(msg);
  else if (msg.message_type == "get_state")
    HandleGetState(msg);
  else if (msg.message_type == "start_hand")
    HandleStartHand(msg);
  else if (msg.message_type == "broadcast_state")
    BroadcastState();
  else
    LOG_WARN("TableActor {}: unknown message type '{}'", state_.table_id, msg.message_type);
}

void TableActor::HandleJoinTable(const concurrency::MessageEnvelope& msg) {
  auto j = json::parse(msg.payload, nullptr, false);
  if (j.is_discarded()) {
    SendError(msg, 1000, "Invalid JSON");
    return;
  }

  std::string session_token = j.value("token", "");
  int32_t player_id = j.value("player_id", -1);
  int seat_index = j.value("seat_index", -1);
  int64_t buy_in = j.value("buy_in", 0);

  if (session_token.empty() || !sessions_.IsAuthorized(session_token, state_.table_id)) {
    SendError(msg, 1001, "Unauthorized");
    return;
  }

  state_.player_session_map[player_id] = session_token;
  state_.session_player_map[session_token] = player_id;

  LOG_INFO("Player {} joined table {}", player_id, state_.table_id);
  BroadcastState();
}

void TableActor::HandleLeaveTable(const concurrency::MessageEnvelope& msg) {
  int32_t player_id = static_cast<int32_t>(msg.sender_id);
  auto it = state_.player_session_map.find(player_id);
  if (it != state_.player_session_map.end()) {
    state_.session_player_map.erase(it->second);
    state_.player_session_map.erase(it);
  }
  LOG_INFO("Player {} left table {}", player_id, state_.table_id);
  BroadcastState();
}

void TableActor::HandlePlayerAction(const concurrency::MessageEnvelope& msg) {
  auto j = json::parse(msg.payload, nullptr, false);
  if (j.is_discarded()) {
    SendError(msg, 1000, "Invalid JSON");
    return;
  }

  std::string action_str = j.value("action", "");
  int64_t amount = j.value("amount", 0);

  LOG_INFO("Player {} action: {} amount={} on table {}", msg.sender_id, action_str, amount,
           state_.table_id);

  // 实际行动处理由 Table 类完成
  BroadcastState();
}

void TableActor::HandleGetState(const concurrency::MessageEnvelope& msg) { BroadcastState(); }

void TableActor::HandleStartHand(const concurrency::MessageEnvelope& msg) {
  state_.game_state.hand_number++;
  state_.game_state.phase = game::GamePhase::Preflop;
  LOG_INFO("Table {} started hand #{}", state_.table_id, state_.game_state.hand_number);
  BroadcastState();
}

void TableActor::BroadcastState() {
  if (!broadcast_cb_) return;

  auto& gs = state_.game_state;
  json j = {
      {"table_id", gs.table_id},
      {"hand_number", gs.hand_number},
      {"phase", static_cast<int>(gs.phase)},
      {"dealer_seat", gs.dealer_seat},
      {"current_bet", gs.current_bet},
      {"pot", gs.pot},
      {"current_player_id", gs.current_player_id},
      {"min_raise", gs.min_raise_size},
      {"status", static_cast<int>(gs.status)},
      {"big_blind", gs.big_blind},
  };

  broadcast_cb_(state_.table_id, j.dump());
}

void TableActor::SendError(const concurrency::MessageEnvelope& original, uint16_t code,
                           const std::string& message) {
  LOG_WARN("TableActor error: code={} msg={}", code, message);
  // 实际实现：通过 broadcast_cb_ 发送错误到客户端
}

}  // namespace poker_engine::network
