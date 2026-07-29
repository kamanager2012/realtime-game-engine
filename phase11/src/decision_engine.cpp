#include "poker_engine/phase11/decision_engine.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase11 {
using namespace poker_engine::range;
using namespace poker_engine::equity;
using namespace poker_engine::evaluator;
using namespace poker_engine::phase8;

// ===================== DecisionResult =====================
std::string DecisionResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "=== Decision Engine Result ===\n";
  oss << "Action:       " << recommended_action << "\n";
  oss << "Confidence:   " << confidence * 100 << "%\n";
  oss << "Equity:       " << equity * 100 << "%\n";
  oss << "EV:           " << expected_value << " BB\n";
  oss << "Exploitability: " << exploitability << " mBB\n";
  oss << "Time:         " << compute_time_ms << " ms\n\n";

  oss << "--- Action EVs ---\n";
  for (const auto& [action, ev] : action_ev) {
    std::string marker = (action == recommended_action) ? "► " : "  ";
    oss << marker << std::setw(10) << std::left << action << std::setw(8) << std::right << ev
        << " BB\n";
  }

  if (!reasoning.empty()) {
    oss << "\nReasoning: " << reasoning << "\n";
  }

  return oss.str();
}

// ===================== DecisionEngine =====================

DecisionEngine::DecisionEngine(DecisionLevel level) : level_(level) {
  fast_solver_ = std::make_unique<FastPreflopSolver>();
  fast_solver_->LoadLUT("/tmp/fast_preflop_lut.bin");
}

void DecisionEngine::SetVillainRange(const std::string& range_str) { villain_range_ = range_str; }

void DecisionEngine::SetLevel(DecisionLevel level) { level_ = level; }
void DecisionEngine::Reset() {}

DecisionResult DecisionEngine::Decide(const GameContext& ctx) {
  auto start = std::chrono::high_resolution_clock::now();
  DecisionResult result;

  if (ctx.street == 0) {
    result = DecidePreFlop(ctx);
  } else {
    result = DecidePostFlop(ctx);
  }

  auto end = std::chrono::high_resolution_clock::now();
  result.compute_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

  return result;
}

DecisionResult DecisionEngine::DecidePreFlop(const GameContext& ctx) {
  DecisionResult result;

  if (level_ == DecisionLevel::QUICK) {
    return QuickDecision(ctx);
  }

  PositionVars opp;
  if (ctx.villain_tendency == "tight")
    opp = {0.12, 0.08, 0.02};
  else if (ctx.villain_tendency == "loose")
    opp = {0.40, 0.25, 0.08};
  else
    opp = {0.25, 0.15, 0.05};

  std::string pos_name;
  const char* names[] = {"UTG", "UTG1", "UTG2", "MP", "MP1", "HJ", "CO", "BTN", "SB", "BB"};
  if (ctx.position < 10)
    pos_name = names[ctx.position];
  else
    pos_name = "MP";

  // Try to analyze specific hand
  if (!ctx.hero_cards.empty() && ctx.hero_cards.size() >= 4) {
    auto advice = fast_solver_->AnalyzeSingleHand(ctx.hero_cards, pos_name, opp);

    result.recommended_action = advice.category;
    result.equity = advice.equity_vs_1bb_range;
    result.expected_value = advice.ev_vs_1bb;
    result.confidence = std::min(1.0, 0.5 + advice.equity_vs_1bb_range);

    result.action_ev.push_back({"Raise", static_cast<double>(advice.ev_vs_1bb) * 2.0});
    result.action_ev.push_back({"Call", static_cast<double>(advice.ev_vs_1bb)});
    result.action_ev.push_back({"Fold", 0.0});

    result.reasoning = "Equity " + std::to_string(int(advice.equity_vs_1bb_range * 100)) + "% vs " +
                       pos_name + " range | EV " + std::to_string(int(advice.ev_vs_1bb * 100)) +
                       " BB/100";

    return result;
  }

  // Fallback: generic from SolvePosition
  auto fast_result = fast_solver_->SolvePosition(pos_name, opp);
  if (!fast_result.advice.empty()) {
    auto& top = fast_result.advice[0];
    result.recommended_action = top.category;
    result.equity = top.equity_vs_1bb_range;
    result.expected_value = top.ev_vs_1bb;
    result.confidence = 0.7;
    result.action_ev.push_back({"Raise", static_cast<double>(top.ev_vs_1bb) * 2.0});
    result.action_ev.push_back({"Call", static_cast<double>(top.ev_vs_1bb)});
    result.action_ev.push_back({"Fold", 0.0});
    result.reasoning = "LUT-based analysis for " + pos_name;
  }

  return result;
}

DecisionResult DecisionEngine::DecidePostFlop(const GameContext& ctx) {
  DecisionResult result;

  Range hero_range, villain_range;

  if (!ctx.hero_cards.empty()) {
    hero_range = Range::FromString(ctx.hero_cards);
  } else {
    hero_range = Range::FullCombinatorial();
  }

  if (!villain_range_.empty()) {
    villain_range = Range::FromString(villain_range_);
  }

  double equity_val = 0.5;
  if (!villain_range_.empty() && hero_range.NonZeroCount() > 0) {
    std::vector<Card> board;
    for (const auto& c : ctx.community_cards) board.push_back(Card::Parse(c));

    uint8_t board5[5] = {0};
    for (size_t i = 0; i < std::min(board.size(), size_t(5)); i++) board5[i] = board[i].Id();

    std::mt19937 rng(42);
    auto res = EquityCalculator::CalculateMonteCarlo(hero_range, villain_range, board5,
                                                     static_cast<int>(board.size()), 5000, rng);
    equity_val = res.equity[0];
  }

  double pot_odds = ctx.to_call / std::max(1.0, ctx.pot + ctx.to_call);
  double ev_call = equity_val * (ctx.pot + ctx.to_call) - (1.0 - equity_val) * ctx.to_call;
  double ev_fold = 0;
  double ev_raise = equity_val * ctx.pot * 2.0 - (1.0 - equity_val) * ctx.pot * 0.5;

  result.equity = equity_val;

  if (ctx.to_call == 0) {
    if (equity_val > 0.6) {
      result.recommended_action = "Bet";
      result.expected_value = ev_raise;
    } else {
      result.recommended_action = "Check";
      result.expected_value = equity_val * ctx.pot;
    }
  } else {
    if (ev_call > ev_fold && ev_call > ev_raise) {
      result.recommended_action = "Call";
      result.expected_value = ev_call;
    } else if (ev_raise > ev_call && ev_raise > ev_fold) {
      result.recommended_action = "Raise";
      result.expected_value = ev_raise;
    } else {
      result.recommended_action = "Fold";
      result.expected_value = ev_fold;
    }
  }

  result.confidence = std::clamp(std::abs(equity_val - 0.5) * 3, 0.1, 1.0);

  result.action_ev = {
      {"Fold", ev_fold}, {"Call", ev_call}, {"Raise", ev_raise}, {"Check", equity_val * ctx.pot}};

  result.exploitability = std::abs(equity_val - 0.5) * 500;

  result.reasoning = "Equity: " + std::to_string(int(equity_val * 100)) +
                     "% | Pot odds: " + std::to_string(int(pot_odds * 100)) +
                     "% | Best: " + result.recommended_action + " (" +
                     std::to_string(int(result.expected_value)) + " BB)";

  return result;
}

DecisionResult DecisionEngine::QuickDecision(const GameContext& ctx) {
  DecisionResult result;

  double equity_val = 0.5;

  if (!villain_range_.empty() && !ctx.hero_cards.empty()) {
    std::vector<Card> board;
    for (const auto& c : ctx.community_cards) board.push_back(Card::Parse(c));

    Range hero = Range::FromString(ctx.hero_cards);
    Range villain = Range::FromString(villain_range_);
    uint8_t board5[5] = {0};
    for (size_t i = 0; i < std::min(board.size(), size_t(5)); i++) board5[i] = board[i].Id();

    std::mt19937 rng(42);
    auto res = EquityCalculator::CalculateMonteCarlo(hero, villain, board5,
                                                     static_cast<int>(board.size()), 500, rng);
    equity_val = res.equity[0];
  }

  double ev_call = equity_val * (ctx.pot + ctx.to_call) - (1.0 - equity_val) * ctx.to_call;

  if (ctx.to_call == 0) {
    result.recommended_action = (equity_val > 0.5) ? "Bet" : "Check";
  } else {
    if (ev_call > 0)
      result.recommended_action = "Call";
    else
      result.recommended_action = "Fold";
  }

  result.equity = equity_val;
  result.expected_value = (result.recommended_action == "Call") ? ev_call : 0;
  result.confidence = 0.5;
  result.reasoning = "Quick EQ estimate: " + std::to_string(int(equity_val * 100)) + "%";

  return result;
}

}  // namespace phase11
}  // namespace poker_engine
