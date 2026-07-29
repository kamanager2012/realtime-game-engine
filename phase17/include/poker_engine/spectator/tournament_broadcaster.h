#include <nlohmann/json.hpp>

#include "spectator_manager.h"
#pragma once

#include <functional>

#include "poker_engine/concurrency/actor.h"
#include "poker_engine/tournament/tournament.h"
#include "spectator_types.h"

namespace poker_engine::spectator {

// ==================== 锦标赛广播 Actor ====================
// 每个锦标赛对应一个广播 Actor，负责收集事件并推送给观众

class TournamentBroadcaster : public concurrency::Actor {
 public:
  TournamentBroadcaster(uint64_t tournament_id, SpectatorManager& spec_mgr);

  void OnHandEvent(uint64_t hand_id, const game::GameAction& action);
  void OnHandComplete(uint64_t hand_id, const game::GameState& state);
  void OnTournamentEvent(const tournament::TournamentEvent& event);
  void OnLeaderboardUpdate();

  void OnReceive(const concurrency::MessageEnvelope& msg) override;

 private:
  uint64_t tournament_id_;
  SpectatorManager& spec_mgr_;
  uint64_t event_counter_ = 0;

  void BroadcastEvent(SpectatorMessageType type, const nlohmann::json& payload);
};

// ==================== 广播工厂 ====================

class BroadcasterFactory {
 public:
  explicit BroadcasterFactory(SpectatorManager& spec_mgr);

  TournamentBroadcaster* GetOrCreate(uint64_t tournament_id);
  void Remove(uint64_t tournament_id);

 private:
  SpectatorManager& spec_mgr_;
  std::unordered_map<uint64_t, std::unique_ptr<TournamentBroadcaster>> broadcasters_;
  std::mutex mutex_;
};

}  // namespace poker_engine::spectator
