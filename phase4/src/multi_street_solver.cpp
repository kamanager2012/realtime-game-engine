#include "poker_engine/phase4/multi_street_solver.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"

namespace poker_engine {
namespace phase4 {

using namespace poker_engine;
using namespace poker_engine::range;
using namespace poker_engine::equity;

// ===================== MS_Node =====================
MS_Node::MS_Node() {
  std::fill(cumulative_regret, cumulative_regret + NUM_MS_ACTIONS, 0.0);
  std::fill(strategy_sum, strategy_sum + NUM_MS_ACTIONS, 0.0);
}

void MS_Node::GetStrategy(double out[NUM_MS_ACTIONS]) const {
  double sum = 0;
  for (int a = 0; a < NUM_MS_ACTIONS; a++) {
    out[a] = MS_PositivePart(cumulative_regret[a]);
    sum += out[a];
  }
  if (sum > 1e-10) {
    for (int a = 0; a < NUM_MS_ACTIONS; a++) out[a] /= sum;
  } else {
    for (int a = 0; a < NUM_MS_ACTIONS; a++) out[a] = 1.0 / NUM_MS_ACTIONS;
  }
}

void MS_Node::GetAverageStrategy(double out[NUM_MS_ACTIONS]) const {
  double sum = 0;
  for (int a = 0; a < NUM_MS_ACTIONS; a++) {
    out[a] = strategy_sum[a];
    sum += out[a];
  }
  if (sum > 1e-10) {
    for (int a = 0; a < NUM_MS_ACTIONS; a++) out[a] /= sum;
  } else {
    for (int a = 0; a < NUM_MS_ACTIONS; a++) out[a] = 1.0 / NUM_MS_ACTIONS;
  }
}

std::string MS_Node::BestActionName() const {
  double avg[NUM_MS_ACTIONS];
  GetAverageStrategy(avg);
  int best = 0;
  for (int a = 1; a < NUM_MS_ACTIONS; a++) {
    if (avg[a] > avg[best]) best = a;
  }
  return MS_ActionName[best];
}

// ===================== MultiStreetSolver =====================
MultiStreetSolver::MultiStreetSolver(const MS_Config& config) : config_(config) {}

void MultiStreetSolver::SetRanges(const std::vector<Range>& ranges) { ranges_ = ranges; }

void MultiStreetSolver::SetFlop(const std::vector<Card>& flop) {
  board_.clear();
  for (const auto& c : flop) board_.push_back(c);
}

void MultiStreetSolver::SetTurn(Card turn) {
  if (board_.size() == 3) board_.push_back(turn);
}

void MultiStreetSolver::SetRiver(Card river) {
  if (board_.size() == 4) board_.push_back(river);
}

double MultiStreetSolver::ComputeEquity(int player, int street_idx, int n_samples) {
  uint8_t board5[5] = {0};
  int bs = 0;
  for (int i = 0; i < static_cast<int>(board_.size()) && i <= street_idx + 1 && i < 5; i++) {
    // street_idx: 0=preflop, 1=flop -> board has 3 cards, etc.
    if (static_cast<int>(board_.size()) > i) {
      board5[i] = board_[i].Id();
      bs = i + 1;
    }
  }

  if (ranges_.size() < 2) return 0.5;

  if (player >= static_cast<int>(ranges_.size())) player = 0;
  int opp = 1 - player;
  if (opp >= static_cast<int>(ranges_.size())) opp = 0;

  auto res = EquityCalculator::CalculateMonteCarlo(ranges_[player], ranges_[opp], board5, bs,
                                                   n_samples, rng_);
  return res.equity[0];
}

double MultiStreetSolver::CFR(int player, double reach_probs[4], int depth, int street_idx,
                              const std::string& history) {
  if (depth > 12) return 0;

  double min_reach = 1e10;
  int n_players = std::min(static_cast<int>(ranges_.size()), 4);
  for (int p = 0; p < n_players; p++) {
    if (reach_probs[p] < min_reach) min_reach = reach_probs[p];
  }
  if (min_reach < 1e-6) return 0;

  // 构建 info set
  MS_InfoSetKey key;
  key.street = street_idx;
  key.history = history;

  // 终局: showdown (simplified)
  if (history.size() >= 2 && history.back() == 's') {
    // showdown: 所有玩家亮牌, equity 决定
    double eq = ComputeEquity(player, street_idx, config_.mc_samples);
    double pot = 15.0 + history.size() * 5.0;  // simplified
    return eq * pot - (1.0 - eq) * (history.size() * 5.0);
  }

  auto& node = node_map_[key];
  node.key = key;
  node.visit_count++;

  double strategy[NUM_MS_ACTIONS];
  node.GetStrategy(strategy);

  // 动作数量取决于街
  int valid_actions = NUM_MS_ACTIONS;
  if (street_idx == 0) valid_actions = NUM_MS_ACTIONS;  // preflop

  // 计算每种行动的 utility
  double util[NUM_MS_ACTIONS] = {0};
  double node_util = 0;

  double equity = ComputeEquity(player, street_idx, config_.mc_samples);
  double pot = 15.0 + depth * 5.0;

  for (int a = 0; a < valid_actions; a++) {
    MS_Action action = static_cast<MS_Action>(a);

    if (action == MS_Action::FOLD) {
      util[a] = -2.0 * (street_idx + 1);  // lose invested
    } else if (action == MS_Action::CHECK || action == MS_Action::CALL) {
      double call_cost = (street_idx + 1) * 5.0;
      util[a] = equity * pot - (1.0 - equity) * call_cost;
    } else if (action == MS_Action::BET_POT) {
      double bet = pot;
      util[a] = equity * (pot + bet) - (1.0 - equity) * bet;
    } else if (action == MS_Action::ALL_IN) {
      double allin = pot * 2;
      util[a] = equity * (pot + allin) - (1.0 - equity) * allin;
    }

    node_util += strategy[a] * util[a];
  }

  // 累积后悔值
  for (int a = 0; a < valid_actions; a++) {
    double regret = util[a] - node_util;
    node.cumulative_regret[a] += regret * reach_probs[player];
  }

  // 累加策略
  for (int a = 0; a < valid_actions; a++) {
    if (player == 0) node.strategy_sum[a] += strategy[a] * reach_probs[0];
  }

  // 递归
  double discount = config_.discount_factor;
  for (int a = 0; a < valid_actions; a++) {
    if (strategy[a] < 1e-6) continue;

    double new_reach[4];
    for (int p = 0; p < n_players; p++) new_reach[p] = reach_probs[p];
    double cf_reach = new_reach[player];
    new_reach[player] *= strategy[a];

    MS_Action action = static_cast<MS_Action>(a);
    std::string new_hist = history + MS_ActionName[a][0];

    if (action == MS_Action::FOLD) {
      // terminal
      continue;
    }

    int new_street = street_idx;
    // 在一定深度推进街
    if (history.size() % 2 == 0 && a == static_cast<int>(MS_Action::CALL)) {
      new_street = std::min(street_idx + 1, 3);
    }

    double child_util = CFR(player, new_reach, depth + 1, new_street, new_hist);
    node_util += strategy[a] * child_util * discount;
  }

  return node_util;
}

MS_SolveResult MultiStreetSolver::SolveFromHand(const HandHistory& hh) {
  MS_SolveResult result;
  result.iterations = config_.iterations;

  // 根据手牌历史设置公共牌
  for (auto& c : hh.all_board_cards) board_.push_back(c);

  // 设置范围
  if (ranges_.empty()) {
    ranges_.push_back(Range::FullCombinatorial());
    ranges_.push_back(Range::FullCombinatorial());
  }

  for (int iter = 0; iter < config_.iterations; iter++) {
    for (int p = 0; p < 2; p++) {
      double reach[4] = {0};
      reach[p] = 1.0;
      CFR(p, reach, 0, 0, "");
    }

    if (config_.verbose && (iter + 1) % 100 == 0) {
      std::cout << "\rMS-CFRA iter " << (iter + 1) << "/" << config_.iterations << std::flush;
    }
  }

  // 提取策略
  for (const auto& [key, node] : node_map_) {
    result.strategies[key] = {};
    node.GetAverageStrategy(result.strategies[key].data());
  }

  // 估算 exploitability
  if (!node_map_.empty()) {
    double total_r = 0;
    int cnt = 0;
    for (const auto& [key, node] : node_map_) {
      double avg[NUM_MS_ACTIONS];
      node.GetAverageStrategy(avg);
      for (int a = 0; a < NUM_MS_ACTIONS; a++) total_r += avg[a] * node.cumulative_regret[a];
      cnt++;
    }
    result.exploitability = total_r / cnt * 1000;
  }

  return result;
}

MS_SolveResult MultiStreetSolver::SolveFullTree() { return SolveFromHand(HandHistory()); }

std::array<double, NUM_MS_ACTIONS> MultiStreetSolver::GetStrategy(const MS_InfoSetKey& key) const {
  auto it = node_map_.find(key);
  if (it == node_map_.end()) {
    std::array<double, NUM_MS_ACTIONS> uniform;
    uniform.fill(1.0 / NUM_MS_ACTIONS);
    return uniform;
  }
  std::array<double, NUM_MS_ACTIONS> out;
  it->second.GetAverageStrategy(out.data());
  return out;
}

// ===================== StrategyVisualization =====================
std::string StrategyVisualization::PrintNode(const MS_InfoSetKey& key,
                                             const std::array<double, NUM_MS_ACTIONS>& strategy) {
  std::ostringstream oss;
  oss << "Street " << key.street << " [" << key.history << "]: ";
  for (int a = 0; a < NUM_MS_ACTIONS; a++) {
    if (strategy[a] > 0.001) oss << MS_ActionName[a] << " " << int(strategy[a] * 100 + 0.5) << "% ";
  }
  return oss.str();
}

std::string StrategyVisualization::PrintAllNodes(
    const std::map<MS_InfoSetKey, std::array<double, NUM_MS_ACTIONS>>& strategies) {
  std::ostringstream oss;
  for (const auto& [key, strat] : strategies) {
    oss << PrintNode(key, strat) << "\n";
  }
  return oss.str();
}

// ===================== MS_SolveResult =====================
std::string MS_SolveResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3);
  oss << "=== Multi-Street CFRA Solve ===\n";
  oss << "Iterations: " << iterations << "\n";
  oss << "Exploitability: " << exploitability << " mBB\n";
  oss << "Nodes: " << strategies.size() << "\n\n";

  oss << "Strategies by info set:\n";
  for (const auto& [key, strat] : strategies) {
    oss << "  S" << key.street << " [" << key.history << "]: ";
    for (int a = 0; a < NUM_MS_ACTIONS; a++) {
      if (strat[a] > 0.001) oss << MS_ActionName[a] << ":" << int(strat[a] * 100 + 0.5) << "% ";
    }
    oss << "\n";
  }
  return oss.str();
}

}  // namespace phase4
}  // namespace poker_engine
