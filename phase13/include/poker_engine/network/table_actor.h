#pragma once

#include "poker_engine/concurrency/actor.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/game/player_state.h"
#include "poker_engine/network/session_manager.h"

namespace poker_engine::network {

enum class TableActorMessageType : uint8_t {
  JoinTable = 0,
  LeaveTable = 1,
  PlayerAction = 2,
  GetState = 3,
  BroadcastState = 4,
  StartHand = 5,
};

struct TableActorState {
  std::string table_id;
  game::GameState game_state;
  std::unordered_map<int32_t, std::string> player_session_map;
  std::unordered_map<std::string, int32_t> session_player_map;
};

class TableActor : public concurrency::Actor {
 public:
  explicit TableActor(const std::string& table_id);

  void OnReceive(const concurrency::MessageEnvelope& msg) override;

  const game::GameState& GetState() const { return state_.game_state; }

  void SetBroadcastCallback(
      std::function<void(const std::string& table_id, const std::string& json)> cb) {
    broadcast_cb_ = std::move(cb);
  }

 private:
  void HandleJoinTable(const concurrency::MessageEnvelope& msg);
  void HandleLeaveTable(const concurrency::MessageEnvelope& msg);
  void HandlePlayerAction(const concurrency::MessageEnvelope& msg);
  void HandleGetState(const concurrency::MessageEnvelope& msg);
  void HandleStartHand(const concurrency::MessageEnvelope& msg);

  void BroadcastState();
  void SendError(const concurrency::MessageEnvelope& original, uint16_t code,
                 const std::string& message);

  TableActorState state_;
  SessionManager& sessions_;

  std::function<void(const std::string& table_id, const std::string& json)> broadcast_cb_;
};

}  // namespace poker_engine::network
