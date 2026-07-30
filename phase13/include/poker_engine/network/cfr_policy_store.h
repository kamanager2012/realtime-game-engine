#pragma once

#include <cstdint>
#include <mutex>
#include <random>
#include <optional>
#include <string>

#include "poker_engine/cfr/cfr_training.h"
#include "poker_engine/game/action.h"
#include "poker_engine/game/game_state.h"

namespace poker_engine::network {

// Global CFR model store for live bot inference.
class CfrPolicyStore {
 public:
  static CfrPolicyStore& Instance();

  bool LoadFromFile(const std::string& path);
  bool IsLoaded() const;
  size_t NodeCount() const;
  std::string ModelPath() const;

  std::optional<poker_engine::game::GameAction> SampleAction(
      const poker_engine::game::GameState& state, int32_t player_id, std::mt19937& rng) const;

  // Observation path: same sampling logic, but keyed off the redacted per-player
  // Observation (viewer_id identifies the actor). Used by the live agent seam.
  std::optional<poker_engine::game::GameAction> SampleAction(
      const poker_engine::game::Observation& obs, std::mt19937& rng) const;

 private:
  CfrPolicyStore() = default;

  mutable std::mutex mu_;
  poker_engine::cfr::Policy policy_;
  std::string model_path_;
  bool loaded_ = false;
};

}  // namespace poker_engine::network
