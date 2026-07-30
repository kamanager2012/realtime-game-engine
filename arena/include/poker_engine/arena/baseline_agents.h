#pragma once
#include <string>

#include "poker_engine/network/ai_engine.h"

namespace poker_engine::arena {

// Honest, exploitable baselines — the standard sparring partners of poker AI.
// Both choose purely from the validator-legal action set handed to Decide(); they
// never read hole cards and carry no RNG, so they are fully deterministic and
// obviously fair. Their known weaknesses (never folding / never slowing down) give
// a round-robin leaderboard a clear skill gradient to calibrate against.

// CallStation: never bets or raises. Checks when it is free, otherwise calls, and
// folds only when it can neither check nor call. Exploited by relentless value bets.
class CallStationAgent : public poker_engine::network::IAIEngine {
 public:
  explicit CallStationAgent(const poker_engine::network::AIConfig& config =
                                poker_engine::network::AIConfig()) : config_(config) {
    if (config_.name.empty() || config_.name == "AI_Bot") config_.name = "CallStation";
  }

  void Initialize(const poker_engine::network::AIConfig& config) override { config_ = config; }
  poker_engine::network::DecisionResponse Decide(
      const poker_engine::network::DecisionRequest& request) override;
  void OnHandComplete(const poker_engine::game::GameState&) override {}
  bool ReloadModel(const std::string&) override { return false; }
  std::string Name() const override { return config_.name; }
  poker_engine::network::AIDifficulty Difficulty() const override {
    return poker_engine::network::AIDifficulty::EASY;
  }

 private:
  poker_engine::network::AIConfig config_;
};

// Maniac: maximum aggression. Prefers RAISE, then BET (both sized all-in), then
// CALL, CHECK, FOLD. Exploited by trapping with strong hands / folding weak ones.
class ManiacAgent : public poker_engine::network::IAIEngine {
 public:
  explicit ManiacAgent(const poker_engine::network::AIConfig& config =
                           poker_engine::network::AIConfig()) : config_(config) {
    if (config_.name.empty() || config_.name == "AI_Bot") config_.name = "Maniac";
  }

  void Initialize(const poker_engine::network::AIConfig& config) override { config_ = config; }
  poker_engine::network::DecisionResponse Decide(
      const poker_engine::network::DecisionRequest& request) override;
  void OnHandComplete(const poker_engine::game::GameState&) override {}
  bool ReloadModel(const std::string&) override { return false; }
  std::string Name() const override { return config_.name; }
  poker_engine::network::AIDifficulty Difficulty() const override {
    return poker_engine::network::AIDifficulty::EASY;
  }

 private:
  poker_engine::network::AIConfig config_;
};

}  // namespace poker_engine::arena
