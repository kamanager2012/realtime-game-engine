#include "poker_engine/phase3/spot_solver.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"

namespace poker_engine {
namespace phase3 {

using namespace poker_engine;
using namespace poker_engine::range;
using poker_engine::equity::EquityCalculator;

SpotSolver::SpotSolver() {}

void SpotSolver::SetHeroRange(const Range& r) { hero_range_ = r; }
void SpotSolver::SetVillainRange(const Range& r) { villain_range_ = r; }
void SpotSolver::SetPot(double pot) { pot_ = pot; }
void SpotSolver::SetToCall(double to_call) { to_call_ = to_call; }
void SpotSolver::SetMode(SolveMode mode) { mode_ = mode; }
void SpotSolver::SetSamples(int samples) { n_samples_ = samples; }

void SpotSolver::SetBoard(const std::vector<std::string>& cards) {
  board_ids_.clear();
  for (const auto& c : cards) {
    board_ids_.push_back(Card::Parse(c).Id());
  }
  cached_equity_ = -1;
}

double SpotSolver::ComputeEquity() {
  if (cached_equity_ >= 0) return cached_equity_;

  uint8_t board5[5] = {0};
  for (size_t i = 0; i < board_ids_.size() && i < 5; i++) board5[i] = board_ids_[i];

  std::mt19937 rng(42);
  auto res = EquityCalculator::CalculateMonteCarlo(
      hero_range_, villain_range_, board5, static_cast<int>(board_ids_.size()), n_samples_, rng);

  cached_equity_ = res.equity[0];
  return cached_equity_;
}

std::vector<SpotAction> SpotSolver::GenerateActions() {
  std::vector<SpotAction> actions;
  double equity = ComputeEquity();

  // Fold
  SpotAction fold;
  fold.desc = "Fold";
  fold.equity = equity;
  fold.ev = -to_call_;
  fold.freq = 0;
  actions.push_back(fold);

  // Check
  SpotAction check;
  check.desc = "Check";
  check.equity = equity;
  check.ev = equity * pot_;
  check.freq = 0;
  actions.push_back(check);

  // Call
  SpotAction call;
  call.desc = "Call $" + std::to_string((int)to_call_);
  call.equity = equity;
  call.ev = equity * (pot_ + to_call_) - (1.0 - equity) * to_call_;
  call.freq = 0;
  actions.push_back(call);

  // Bet half pot
  double half_pot = pot_ * 0.5;
  SpotAction bet_half;
  bet_half.desc = "Bet $" + std::to_string((int)half_pot);
  bet_half.equity = equity;
  bet_half.ev = equity * (pot_ + half_pot) - (1.0 - equity) * half_pot;
  bet_half.freq = 0;
  actions.push_back(bet_half);

  // Bet pot
  SpotAction bet_pot;
  bet_pot.desc = "Bet $" + std::to_string((int)pot_);
  bet_pot.equity = equity;
  bet_pot.ev = equity * (pot_ + pot_) - (1.0 - equity) * pot_;
  bet_pot.freq = 0;
  actions.push_back(bet_pot);

  // All-in (2x pot)
  double allin = pot_ * 2.0;
  SpotAction allin_action;
  allin_action.desc = "All-in $" + std::to_string((int)allin);
  allin_action.equity = equity;
  allin_action.ev = equity * (pot_ + allin) - (1.0 - equity) * allin;
  allin_action.freq = 0;
  actions.push_back(allin_action);

  return actions;
}

SpotAction SpotSolver::BestByEV(const std::vector<SpotAction>& actions) {
  SpotAction best = actions[0];
  for (const auto& a : actions) {
    if (a.ev > best.ev) best = a;
  }
  return best;
}

SpotResult SpotSolver::Solve() {
  SpotResult result;
  result.hero_range_str = std::to_string(hero_range_.NonZeroCount()) + " combos";
  result.villain_range_str = std::to_string(villain_range_.NonZeroCount()) + " combos";
  result.pot = pot_;
  result.to_call = to_call_;

  for (auto id : board_ids_) {
    result.board_str += Card(id).ToString() + " ";
  }

  auto actions = GenerateActions();

  if (mode_ == SolveMode::PureExploit) {
    auto best = BestByEV(actions);
    for (auto& a : actions) {
      a.freq = (a.desc == best.desc) ? 1.0 : 0.0;
      a.is_gto_best = (a.desc == best.desc);
    }
  } else if (mode_ == SolveMode::MixedGTO) {
    double total_positive_ev = 0;
    for (auto& a : actions) {
      if (a.ev > 0) {
        a.freq = a.ev;
        total_positive_ev += a.ev;
      } else {
        a.freq = 0;
      }
    }
    if (total_positive_ev > 0) {
      for (auto& a : actions) a.freq /= total_positive_ev;
    }
    auto best = BestByEV(actions);
    for (auto& a : actions) a.is_gto_best = (a.desc == best.desc);
  } else {
    for (auto& a : actions) {
      a.freq = (a.ev > -pot_ * 0.1) ? 0.5 : 0.0;
    }
  }

  result.actions = actions;
  result.best_action = &result.actions[0];
  for (auto& a : result.actions) {
    if (a.ev > result.best_action->ev) {
      result.best_action = &a;
    }
  }
  result.hero_ev = result.best_action->ev;
  result.strategy_ev = 0;
  for (const auto& a : result.actions) {
    result.strategy_ev += a.freq * a.ev;
  }

  return result;
}

std::string SpotResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "=== Spot Solve Result ===\n";
  oss << "Board: " << board_str << "\n";
  oss << "Pot: $" << pot << " | To call: $" << to_call << "\n";
  oss << "Hero range: " << hero_range_str << "\n";
  oss << "Villain range: " << villain_range_str << "\n\n";

  oss << "Actions:\n";
  for (const auto& a : actions) {
    oss << "  " << (a.is_gto_best ? "★ " : "  ");
    oss << std::setw(18) << std::left << a.desc << " EV=$" << std::setw(8) << std::right << a.ev
        << " Eq=" << std::setw(5) << int(a.equity * 100) << "%"
        << " Freq=" << std::setw(5) << int(a.freq * 100) << "%\n";
  }
  oss << "\nBest action: " << best_action->desc << " (EV=$" << best_action->ev << ")\n";
  return oss.str();
}

SpotResult SpotSolver::QuickSolve(const std::string& hero_range, const std::string& villain_range,
                                  const std::string& board, double pot, double to_call) {
  SpotSolver solver;
  solver.SetHeroRange(Range::FromString(hero_range));
  solver.SetVillainRange(Range::FromString(villain_range));

  std::vector<std::string> cards;
  for (size_t i = 0; i + 1 < board.size(); i += 2) {
    cards.push_back(board.substr(i, 2));
  }
  solver.SetBoard(cards);
  solver.SetPot(pot);
  solver.SetToCall(to_call);
  solver.SetSamples(20000);

  return solver.Solve();
}

}  // namespace phase3
}  // namespace poker_engine
