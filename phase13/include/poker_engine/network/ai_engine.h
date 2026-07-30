#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "poker_engine/game/action.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/game/observation.h"

namespace poker_engine::network {

using game::ActionType;
using game::GameAction;
using game::GameState;
using game::Observation;

enum class AIDifficulty { EASY, MEDIUM, HARD, EXPERT };

enum class AIStrategyType { RuleBased, CfrModel };

struct AIConfig {
  AIStrategyType strategy = AIStrategyType::RuleBased;
  std::string model_path;
  AIDifficulty difficulty = AIDifficulty::MEDIUM;
  std::string name = "AI_Bot";
  double aggression = 0.5;
  double bluff_frequency = 0.15;
  bool use_lut = true;
  int random_seed = 42;
  int64_t time_limit_ms = 5000;  // max decision time (ADR-004)
};

// ADR-004: Decision request carrying a REDACTED per-player observation.
// The agent receives a game::Observation (public table state + only its own
// hole cards). Opponents' hole cards are not representable in this type, so an
// agent cannot peek at hidden information through Decide().
struct DecisionRequest {
  game::Observation observation;
  int32_t player_id;
  std::vector<GameAction> legal_actions;
};

// ADR-004: Decision response with confidence and timing.
struct DecisionResponse {
  GameAction action;
  int64_t decision_time_ms = 0;
  float confidence = 0.0f;
  std::string reason;
};

// ADR-004: Abstract AI engine interface.
// Enables swapping between rule-based, CFR-based, and external AI implementations
// without the game engine knowing which is active.
class IAIEngine {
 public:
  virtual ~IAIEngine() = default;

  /// Initialize / reload configuration.
  virtual void Initialize(const AIConfig& config) = 0;

  /// Request a decision for the given player in the current game state.
  /// Must complete within AIConfig::time_limit_ms (caller enforces timeout).
  virtual DecisionResponse Decide(const DecisionRequest& request) = 0;

  /// Notification that a hand has completed (for learning / statistics).
  virtual void OnHandComplete(const GameState& final_state) = 0;

  /// Hot-reload the AI model from disk.
  virtual bool ReloadModel(const std::string& model_path) = 0;

  /// Human-readable name for logging / UI.
  virtual std::string Name() const = 0;
  virtual AIDifficulty Difficulty() const = 0;
};

// Concrete AI engine implementing IAIEngine.
// Supports RuleBased and CfrModel strategies with adjustable difficulty.
class AIEngine : public IAIEngine {
 public:
  explicit AIEngine(const AIConfig& config = AIConfig());

  // ---- IAIEngine interface (ADR-004) ----
  void Initialize(const AIConfig& config) override;
  DecisionResponse Decide(const DecisionRequest& request) override;
  void OnHandComplete(const GameState& final_state) override;
  bool ReloadModel(const std::string& model_path) override;
  std::string Name() const override { return config_.name; }
  AIDifficulty Difficulty() const override { return config_.difficulty; }

  // ---- Legacy API (kept for backward compatibility) ----
  // Trusted server path: builds a redacted Observation via ObserveFor and routes
  // through the same decision core as Decide().
  GameAction MakeDecision(const GameState& game, int32_t player_id);

  void OnActionTaken(int32_t player_id, ActionType action, double amount);
  void OnHandStart();
  void OnStreetChange(int street);

  void SetDifficulty(AIDifficulty diff);
  void SetConfig(const AIConfig& cfg) {
    config_ = cfg;
    rng_.seed(cfg.random_seed);
  }

 private:
  AIConfig config_;
  std::mt19937 rng_;

  // Shared decision core consumed by both Decide() and legacy MakeDecision().
  GameAction DecideFromObservation(const Observation& obs);

  double CalculateEquity(const Observation& obs, int n_samples = 1000);
  bool ShouldBluff();
  GameAction CreateAction(ActionType type, double amount = 0);
  GameAction PreflopDecision(const Observation& obs);
  GameAction PostflopDecision(const Observation& obs);
  double CalculateBetSize(double pot, double to_call);
  std::optional<GameAction> TryCfrDecision(const Observation& obs);
};

// Factory function for creating AI engines by strategy type.
std::unique_ptr<IAIEngine> CreateAIEngine(const AIConfig& config);

}  // namespace poker_engine::network
