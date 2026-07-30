#pragma once
#include <random>
#include <string>

#include "poker_engine/network/ai_engine.h"

namespace poker_engine::arena {

// RandomAgent — the honest lower-bound baseline for the research environment.
// It plays uniformly at random over the set of *legal* actions reported by
// GameState::LegalActions, and, when it chooses to bet/raise, sizes uniformly
// between the minimum legal amount and all-in. It is deterministic for a fixed
// AIConfig::random_seed so benchmarks are reproducible.
class RandomAgent : public poker_engine::network::IAIEngine {
 public:
  explicit RandomAgent(const poker_engine::network::AIConfig& config =
                           poker_engine::network::AIConfig());

  void Initialize(const poker_engine::network::AIConfig& config) override;
  poker_engine::network::DecisionResponse Decide(
      const poker_engine::network::DecisionRequest& request) override;
  void OnHandComplete(const poker_engine::game::GameState& final_state) override;
  bool ReloadModel(const std::string& model_path) override;
  std::string Name() const override { return config_.name; }
  poker_engine::network::AIDifficulty Difficulty() const override {
    return poker_engine::network::AIDifficulty::EASY;
  }

 private:
  poker_engine::network::AIConfig config_;
  std::mt19937_64 rng_;
};

}  // namespace poker_engine::arena
