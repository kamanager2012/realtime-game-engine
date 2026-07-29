// Evaluator integration — replaces stub EvaluateShowdown in CFR engine
// This file documents how to integrate HandEvaluator into the CFR pipeline.
// Apply these changes to the existing cfr_engine.cpp when ready.

#include <memory>
#include <mutex>

#include "poker_engine/evaluator/hand_evaluator.h"

namespace poker_engine::cfr {

// Global evaluator singleton (thread-safe lazy init)
static std::mutex g_eval_mutex;
static std::unique_ptr<evaluator::HandEvaluator> g_evaluator;

// Call during CFREngine::Initialize()
void CFREngine::InitializeEvaluator() {
  std::lock_guard<std::mutex> lock(g_eval_mutex);
  if (!g_evaluator) {
    g_evaluator = std::make_unique<evaluator::HandEvaluator>();
  }
}

// ==================== Real Terminal Value Computation ====================

double CFREngine::TerminalValue(const GameState& state, int player) const {
  if (state.winners.empty()) {
    return EvaluateShowdown(state, player);
  }

  double total_won = 0.0;
  bool is_winner = false;

  for (auto w : state.winners) {
    if (w == player) {
      is_winner = true;
      total_won = state.pot / static_cast<double>(state.winners.size());
    }
  }

  double invested = 0.0;
  // Compute player's total investment from state

  return is_winner ? (total_won - invested) : (-invested);
}

// ==================== Real Showdown Evaluation ====================

double CFREngine::EvaluateShowdown(const GameState& state, int player) const {
  if (!g_evaluator) return 0.0;

  int best_rank = -1;
  std::vector<int> winners;
  double best_strength = -1;

  for (const auto& p : state.players) {
    if (!p.active || p.folded) continue;

    // Collect hole cards + community cards
    uint8_t all_cards[7];
    int card_count = 0;

    // Add hole cards
    for (auto hc : p.hole_cards) {
      if (card_count < 7) all_cards[card_count++] = static_cast<uint8_t>(hc);
    }

    // Add community cards
    for (auto cc : state.community_cards) {
      if (card_count < 7) all_cards[card_count++] = static_cast<uint8_t>(cc);
    }

    if (card_count < 5) continue;

    auto result = g_evaluator->Evaluate(all_cards, card_count);

    if (result.strength > best_strength) {
      best_strength = result.strength;
      winners.clear();
      winners.push_back(p.player_id);
    } else if (result.strength == best_strength) {
      winners.push_back(p.player_id);
    }
  }

  if (winners.empty()) return 0.0;

  bool is_winner = std::find(winners.begin(), winners.end(), player) != winners.end();
  double pot_share = state.pot / static_cast<double>(winners.size());

  double invested = 0.0;
  // Compute from state

  return pot_share - invested;
}

// ==================== Equity Simulation ====================

double CFREngine::SimulateEquity(uint8_t c1, uint8_t c2, const uint8_t community[5], int comm_count,
                                 int num_opponents) const {
  if (!g_evaluator) return 0.5;
  return g_evaluator->GetEquity(c1, c2, community, comm_count, num_opponents);
}

}  // namespace poker_engine::cfr
