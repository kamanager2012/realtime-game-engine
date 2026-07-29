#include "poker_engine/phase2/solver_node.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <random>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/evaluator/evaluator.h"

namespace poker_engine {
namespace phase2 {
namespace {

using namespace poker_engine;
using namespace poker_engine::range;

}  // namespace

// ===================== CFranode =====================
CFranode::CFranode() {
  std::memset(cumulative_regret, 0, sizeof(cumulative_regret));
  std::memset(strategy_sum, 0, sizeof(strategy_sum));
}

void CFranode::GetStrategy(double out_strategy[NUM_ACTIONS]) const {
  double sum = 0;
  for (int a = 0; a < NUM_ACTIONS; a++) {
    out_strategy[a] = (cumulative_regret[a] > 0 ? cumulative_regret[a] : 0);
    sum += out_strategy[a];
  }
  if (sum > 0) {
    for (int a = 0; a < NUM_ACTIONS; a++) out_strategy[a] /= sum;
  } else {
    for (int a = 0; a < NUM_ACTIONS; a++) out_strategy[a] = 1.0 / NUM_ACTIONS;
  }
}

void CFranode::GetAverageStrategy(double out_strategy[NUM_ACTIONS]) const {
  double sum = 0;
  for (int a = 0; a < NUM_ACTIONS; a++) {
    out_strategy[a] = strategy_sum[a];
    sum += out_strategy[a];
  }
  if (sum > 0) {
    for (int a = 0; a < NUM_ACTIONS; a++) out_strategy[a] /= sum;
  } else {
    for (int a = 0; a < NUM_ACTIONS; a++) out_strategy[a] = 1.0 / NUM_ACTIONS;
  }
}

Action CFranode::SampleAction(double strategy[NUM_ACTIONS], double roll) const {
  double cum = 0;
  for (int a = 0; a < NUM_ACTIONS; a++) {
    cum += strategy[a];
    if (roll <= cum) return static_cast<Action>(a);
  }
  return static_cast<Action>(NUM_ACTIONS - 1);
}

std::string CFranode::BestAction() const {
  double avg[NUM_ACTIONS];
  GetAverageStrategy(avg);
  int best = 0;
  for (int a = 1; a < NUM_ACTIONS; a++) {
    if (avg[a] > avg[best]) best = a;
  }
  return ActionName[best];
}

// ===================== CFRSolver =====================
CFRSolver::CFRSolver(const SolverConfig& config) : config_(config) {}

void CFRSolver::SetRanges(const RangeVector& ranges) { ranges_ = ranges; }

void CFRSolver::SetBoard(const std::vector<Card>& board, int street) { board_ = board; }

SolveResult CFRSolver::Solve() {
  SolveResult result;
  result.iterations = config_.iterations;

  for (int iter = 0; iter < config_.iterations; iter++) {
    for (int p = 0; p < 2; p++) {
      double reach[4] = {0};
      reach[0] = 1.0;
      reach[1] = 1.0;
      double node_ev = CFR(p, reach, 0, 0);
      if (p == 0) result.total_ev += node_ev;
    }

    if (config_.verbose && (iter + 1) % 100 == 0) {
      std::cout << "\rIter " << (iter + 1) << "/" << config_.iterations << " EV=" << std::fixed
                << std::setprecision(3) << result.total_ev / (iter + 1) * 1000 << " mBB"
                << std::flush;
    }
  }

  for (auto& [info, node] : node_map_) {
    double avg[NUM_ACTIONS];
    node.GetAverageStrategy(avg);
    result.strategy_profile[info] = {};
    for (int a = 0; a < NUM_ACTIONS; a++) result.strategy_profile[info][a] = avg[a];
  }

  if (!node_map_.empty()) {
    double total_regret = 0;
    int count = 0;
    for (auto& [info, node] : node_map_) {
      double avg[NUM_ACTIONS];
      node.GetAverageStrategy(avg);
      for (int a = 0; a < NUM_ACTIONS; a++) total_regret += avg[a] * node.cumulative_regret[a];
      count++;
    }
    result.exploitability = total_regret / count * 1000;
  }

  return result;
}

double CFRSolver::CFR(int player, double reach_probs[4], int depth, int street_idx) {
  if (depth > 10) return 0;
  if (reach_probs[0] < 0.001 || reach_probs[1] < 0.001) return 0;

  InfoSet is;
  is.street = street_idx;
  is.key = "s" + std::to_string(street_idx) + "_d" + std::to_string(depth);
  is.pot = 15.0 + depth * 10;
  is.to_call = depth > 0 ? 25.0 * depth : 0;

  auto& node = node_map_[is];
  node.visit_count++;

  double strategy[NUM_ACTIONS];
  node.GetStrategy(strategy);

  double util[NUM_ACTIONS] = {0};
  double node_util = 0;

  double equity = 0.5;
  if (street_idx == 1 && board_.size() >= 3) {
    uint8_t board5[5] = {0};
    for (size_t i = 0; i < 3 && i < board_.size(); i++) board5[i] = board_[i].Id();

    if (player < static_cast<int>(ranges_.size()) && ranges_.size() >= 2) {
      std::mt19937 rng(42);  // deterministic solver equity sampling
      auto eq = equity::EquityCalculator::CalculateMonteCarlo(
          ranges_[player], ranges_[1 - player], board5, 3, config_.n_samples, rng);
      equity = eq.equity[0];
    }
  }

  for (int a = 0; a < NUM_ACTIONS; a++) {
    if (a == static_cast<int>(Action::FOLD)) {
      util[a] = -is.to_call;
    } else if (a == static_cast<int>(Action::CHECK) || a == static_cast<int>(Action::CALL)) {
      double win = equity * is.pot;
      double lose = (1.0 - equity) * is.to_call;
      util[a] = win - lose;
    } else {
      double bet_size = 0;
      switch (static_cast<Action>(a)) {
        case Action::BET_33:
          bet_size = is.pot * 0.33;
          break;
        case Action::BET_67:
          bet_size = is.pot * 0.67;
          break;
        case Action::POT:
          bet_size = is.pot;
          break;
        case Action::ALL_IN:
          bet_size = 100;
          break;
        default:
          break;
      }
      util[a] = equity * (is.pot + bet_size) - (1.0 - equity) * bet_size;
    }
    node_util += strategy[a] * util[a];
  }

  for (int a = 0; a < NUM_ACTIONS; a++) {
    double regret = util[a] - node_util;
    node.cumulative_regret[a] += regret * reach_probs[player];
  }

  for (int a = 0; a < NUM_ACTIONS; a++) {
    if (player == 0) node.strategy_sum[a] += strategy[a] * reach_probs[0];
  }

  double new_reach[4];
  for (int a = 0; a < NUM_ACTIONS; a++) {
    if (strategy[a] < 0.001) continue;
    for (int p = 0; p < 4; p++) new_reach[p] = reach_probs[p];
    if (static_cast<Action>(a) == Action::FOLD) {
      double fold_util = -is.to_call;
      node.strategy_sum[a] += fold_util * reach_probs[1 - player];
      continue;
    }
    double child_util = CFR(player, new_reach, depth + 1, street_idx);
    (void)child_util;
  }

  return node_util;
}

std::string SolveResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "CFRA Solver Results\n";
  oss << "===================\n";
  oss << "Iterations: " << iterations << "\n";
  oss << "Exploitability: " << exploitability << " mBB\n";
  oss << "Total EV (hero): $" << total_ev << "\n";
  oss << "Nodes: " << strategy_profile.size() << "\n\n";

  for (const auto& [info, strategy] : strategy_profile) {
    oss << "[Street " << info.street << "] " << info.key << " (pot:$" << info.pot << ", to_call:$"
        << info.to_call << ")\n";
    for (int a = 0; a < NUM_ACTIONS; a++) {
      if (strategy[a] > 0.01f)
        oss << "  " << ActionName[a] << ": " << int(strategy[a] * 100 + 0.5) << "%\n";
    }
    oss << "\n";
  }
  return oss.str();
}

}  // namespace phase2
}  // namespace poker_engine
