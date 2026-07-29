#include <optional>
#include "poker_engine/network/ai_engine.h"

#include "poker_engine/network/cfr_policy_store.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>

#include "poker_engine/evaluator/card.h"
#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/range/hand_id.h"

namespace poker_engine::network {
using namespace poker_engine::game;

AIEngine::AIEngine(const AIConfig& config) : config_(config), rng_(config.random_seed) {}

// ---- IAIEngine interface (ADR-004) ----

void AIEngine::Initialize(const AIConfig& config) {
  config_ = config;
  rng_.seed(config.random_seed);
}

DecisionResponse AIEngine::Decide(const DecisionRequest& request) {
  DecisionResponse resp;
  auto start = std::chrono::steady_clock::now();

  resp.action = MakeDecision(request.state, request.player_id);

  auto end = std::chrono::steady_clock::now();
  resp.decision_time_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  resp.confidence = (config_.difficulty >= AIDifficulty::HARD) ? 0.6f : 0.3f;
  resp.reason = config_.name + " (" +
                (config_.strategy == AIStrategyType::CfrModel ? "CFR" : "rule") + ")";
  return resp;
}

void AIEngine::OnHandComplete(const GameState& final_state) {
  // Stub for future learning — accumulate hand results for CFR training.
  (void)final_state;
}

bool AIEngine::ReloadModel(const std::string& model_path) {
  if (config_.strategy != AIStrategyType::CfrModel) return false;
  config_.model_path = model_path;
  return true;
}

// ---- Legacy API ----

void AIEngine::SetDifficulty(AIDifficulty diff) {
  config_.difficulty = diff;
  switch (diff) {
    case AIDifficulty::EASY:
      config_.aggression = 0.35;
      config_.bluff_frequency = 0.06;
      break;
    case AIDifficulty::MEDIUM:
      config_.aggression = 0.55;
      config_.bluff_frequency = 0.12;
      break;
    case AIDifficulty::HARD:
      config_.aggression = 0.72;
      config_.bluff_frequency = 0.18;
      break;
    case AIDifficulty::EXPERT:
      config_.aggression = 0.85;
      config_.bluff_frequency = 0.24;
      break;
  }
}

void AIEngine::OnActionTaken(int32_t, ActionType, double) {}
void AIEngine::OnHandStart() {}
void AIEngine::OnStreetChange(int) {}


std::optional<GameAction> AIEngine::TryCfrDecision(const GameState& game, int32_t player_id) {
  if (config_.strategy != AIStrategyType::CfrModel) return std::nullopt;
  if (!CfrPolicyStore::Instance().IsLoaded()) return std::nullopt;
  return CfrPolicyStore::Instance().SampleAction(game, player_id, rng_);
}

GameAction AIEngine::MakeDecision(const GameState& game, int32_t player_id) {
  // Find player
  const PlayerState* me = nullptr;
  for (const auto& p : game.AllPlayers()) {
    if (p.id == player_id) {
      me = &p;
      break;
    }
  }
  if (!me || !me->IsActive()) {
    auto a = CreateAction(ActionType::FOLD);
    a.player_id = player_id;
    return a;
  }

  if (auto cfr = TryCfrDecision(game, player_id)) {
    return *cfr;
  }

  double to_call = game.GetCurrentBet() - me->bet_info.current_bet;
  if (to_call < 0) to_call = 0;
  double equity = CalculateEquity(game, player_id, 500);

  std::uniform_real_distribution<double> dist(0.0, 1.0);
  double roll = dist(rng_);

  GameAction decision;
  if (game.GetPhase() == GamePhase::PREFLOP_BETTING) {
    decision = PreflopDecision(game, player_id);
  } else {
    decision = PostflopDecision(game, player_id);
  }
  decision.player_id = player_id;
  return decision;
}

GameAction AIEngine::PreflopDecision(const GameState& game, int32_t player_id) {
  const PlayerState* me = nullptr;
  for (const auto& p : game.AllPlayers()) {
    if (p.id == player_id) {
      me = &p;
      break;
    }
  }
  if (!me) return CreateAction(ActionType::FOLD);

  double to_call = game.GetCurrentBet() - me->bet_info.current_bet;
  if (to_call < 0) to_call = 0;
  double equity = CalculateEquity(game, player_id, 500);
  double pot = game.GetPot();

  std::uniform_real_distribution<double> dist(0.0, 1.0);
  double roll = dist(rng_);

  // No bet to call
  if (to_call < 0.001) {
    if (equity > 0.65) return CreateAction(ActionType::BET, pot * 0.75);
    if (equity > 0.5 && roll < 0.4) return CreateAction(ActionType::BET, pot * 0.5);
    return CreateAction(ActionType::CHECK);
  }

  // Need to call
  if (equity > 0.65) {
    if (roll < 0.6) return CreateAction(ActionType::RAISE, to_call * 2.5 + game.GetCurrentBet());
    return CreateAction(ActionType::CALL);
  }
  if (equity > 0.45) {
    return CreateAction(ActionType::CALL);
  }
  // Weak hand
  if (to_call / me->chips < 0.05) return CreateAction(ActionType::CALL);
  return CreateAction(ActionType::FOLD);
}

GameAction AIEngine::PostflopDecision(const GameState& game, int32_t player_id) {
  const PlayerState* me = nullptr;
  for (const auto& p : game.AllPlayers()) {
    if (p.id == player_id) {
      me = &p;
      break;
    }
  }
  if (!me) return CreateAction(ActionType::FOLD);

  double to_call = game.GetCurrentBet() - me->bet_info.current_bet;
  if (to_call < 0) to_call = 0;
  double equity = CalculateEquity(game, player_id, 800);
  double pot = game.GetPot();
  bool bluff = ShouldBluff();

  std::uniform_real_distribution<double> dist(0.0, 1.0);
  double roll = dist(rng_);

  // No bet
  if (to_call < 0.001) {
    if (bluff && equity < 0.4) {
      return CreateAction(ActionType::BET, pot * (0.4 + dist(rng_) * 0.3));
    }
    if (equity > 0.6) return CreateAction(ActionType::BET, pot * (0.5 + dist(rng_) * 0.3));
    if (equity > 0.4 && config_.aggression > 0.5 && roll < 0.3)
      return CreateAction(ActionType::BET, pot * 0.4);
    return CreateAction(ActionType::CHECK);
  }

  // Facing bet
  double pot_odds = to_call / std::max(1.0, pot + to_call);

  if (equity > 0.7) {
    if (roll < 0.5) return CreateAction(ActionType::RAISE, to_call * 2 + game.GetCurrentBet());
    return CreateAction(ActionType::CALL);
  }
  if (equity > 0.5) {
    if (pot_odds < 0.35) return CreateAction(ActionType::CALL);
    return CreateAction(ActionType::FOLD);
  }
  if (equity > 0.3 && pot_odds < 0.15) return CreateAction(ActionType::CALL);
  return CreateAction(ActionType::FOLD);
}

double AIEngine::CalculateEquity(const GameState& game, int32_t player_id, int n_samples) {
  const PlayerState* me = nullptr;
  for (const auto& p : game.AllPlayers()) {
    if (p.id == player_id) {
      me = &p;
      break;
    }
  }
  if (!me || !me->hole_cards.IsDealt()) return 0.5;

  const uint8_t c1 = me->hole_cards.card1();
  const uint8_t c2 = me->hole_cards.card2();

  // Fast preflop heuristic for EASY / low sample budgets
  if (game.GetPhase() == GamePhase::PREFLOP_BETTING &&
      config_.difficulty == AIDifficulty::EASY) {
    uint8_t r1 = c1 >> 2;
    uint8_t r2 = c2 >> 2;
    bool paired = (r1 == r2);
    bool suited = (c1 % 4 == c2 % 4);
    double high_card = std::max(r1, r2) / 12.0;
    double equity = 0.3 + high_card * 0.3;
    if (paired) equity += 0.15;
    if (suited) equity += 0.05;
    if (r1 >= 10 && r2 >= 10) equity += 0.1;
    return std::min(equity, 0.95);
  }

  int samples = n_samples;
  switch (config_.difficulty) {
    case AIDifficulty::EASY:
      samples = std::min(samples, 120);
      break;
    case AIDifficulty::MEDIUM:
      samples = std::min(samples, 350);
      break;
    case AIDifficulty::HARD:
      samples = std::min(samples, 700);
      break;
    case AIDifficulty::EXPERT:
      samples = std::min(samples, 1200);
      break;
  }

  using poker_engine::range::HandId;
  using poker_engine::range::Range;
  using poker_engine::equity::EquityCalculator;

  Range hero;
  hero.Set(HandId::Encode(c1, c2), 1.0f);
  Range villain = Range::FullCombinatorial();

  const auto& community = game.GetCommunity();
  uint8_t board[5] = {0};
  int board_size = static_cast<int>(community.count);
  for (int i = 0; i < board_size; ++i) board[i] = community.cards[i];

  hero.RemoveBoard(std::vector<uint8_t>(board, board + board_size));
  villain.RemoveBoard(std::vector<uint8_t>(board, board + board_size));
  villain.RemoveHand(c1, c2);

  auto result = EquityCalculator::CalculateMonteCarlo(hero, villain, board, board_size, samples, rng_);
  return static_cast<double>(result.equity[0]);
}

bool AIEngine::ShouldBluff() {
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  return dist(rng_) < config_.bluff_frequency;
}

GameAction AIEngine::CreateAction(ActionType type, double amount) {
  GameAction a;
  a.type = type;
  a.amount = amount;
  a.player_id = 0;
  return a;
}

double AIEngine::CalculateBetSize(double pot, double to_call) {
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  return std::max(to_call, pot * (0.3 + dist(rng_) * 0.5));
}

std::unique_ptr<IAIEngine> CreateAIEngine(const AIConfig& config) {
  return std::make_unique<AIEngine>(config);
}

}  // namespace poker_engine::network
