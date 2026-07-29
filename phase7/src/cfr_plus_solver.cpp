#include "poker_engine/phase7/cfr_plus_solver.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"

namespace poker_engine {
namespace phase7 {
using namespace poker_engine::range;
using Card = poker_engine::Card;
using EquityCalculator = poker_engine::equity::EquityCalculator;

CFRNode::CFRNode() {
  std::fill(cumulative_regret, cumulative_regret + NUM_CFR_ACTIONS, 0.0);
  std::fill(strategy_sum, strategy_sum + NUM_CFR_ACTIONS, 0.0);
  std::fill(positive_regret_sum, positive_regret_sum + NUM_CFR_ACTIONS, 0.0);
}

void CFRNode::GetRegretMatchedStrategy(double out[NUM_CFR_ACTIONS]) const {
  double sum = 0;
  for (int a = 0; a < NUM_CFR_ACTIONS; a++) {
    out[a] = (cumulative_regret[a] > 0) ? cumulative_regret[a] : 0;
    sum += out[a];
  }
  if (sum > 1e-10) {
    for (int a = 0; a < NUM_CFR_ACTIONS; a++) out[a] /= sum;
  } else {
    for (int a = 0; a < NUM_CFR_ACTIONS; a++) out[a] = 1.0 / NUM_CFR_ACTIONS;
  }
}

void CFRNode::GetAverageStrategy(double out[NUM_CFR_ACTIONS]) const {
  double sum = 0;
  for (int a = 0; a < NUM_CFR_ACTIONS; a++) {
    out[a] = strategy_sum[a];
    sum += out[a];
  }
  if (sum > 1e-10) {
    for (int a = 0; a < NUM_CFR_ACTIONS; a++) out[a] /= sum;
  } else {
    for (int a = 0; a < NUM_CFR_ACTIONS; a++) out[a] = 1.0 / NUM_CFR_ACTIONS;
  }
}

double CFRNode::GetAverageEV() const {
  double avg[NUM_CFR_ACTIONS];
  GetAverageStrategy(avg);
  double ev = 0;
  for (int a = 0; a < NUM_CFR_ACTIONS; a++) ev += avg[a] * cumulative_regret[a];
  return ev;
}

std::string CFRNode::BestAction() const {
  double avg[NUM_CFR_ACTIONS];
  GetAverageStrategy(avg);
  int best = 0;
  for (int a = 1; a < NUM_CFR_ACTIONS; a++)
    if (avg[a] > avg[best]) best = a;
  return CFRActionName[best];
}

void CFRNode::ApplyDiscount(double alpha, double beta, double gamma) {
  for (int a = 0; a < NUM_CFR_ACTIONS; a++) {
    if (cumulative_regret[a] > 0)
      cumulative_regret[a] *= alpha;
    else
      cumulative_regret[a] *= beta;
    cumulative_regret[a] = std::max(-1e8, std::min(1e8, cumulative_regret[a]));
    positive_regret_sum[a] *= gamma;
  }
}

CFRPlusSolver::CFRPlusSolver(const CFRConfig& config) : config_(config) {}
void CFRPlusSolver::SetHeroRange(const Range& r) { hero_range_ = r; }
void CFRPlusSolver::SetVillainRange(const Range& r) { villain_range_ = r; }
void CFRPlusSolver::SetPot(double pot) { pot_ = pot; }
void CFRPlusSolver::SetToCall(double to_call) { to_call_ = to_call; }

void CFRPlusSolver::SetBoard(const std::vector<poker_engine::Card>& board) { board_ = board; }

double CFRPlusSolver::getDiscount(int iter, const std::string& type) const {
  if (config_.mode == CFRMode::VANILLA) return 1.0;
  double t = static_cast<double>(iter);
  if (type == "regret_plus") {
    if (config_.mode == CFRMode::CHANCE_SAMPLED) return 1.0;
    return std::pow(t / (t + 1), config_.alpha);
  } else if (type == "avg_strategy") {
    return std::pow(t / (t + 1), config_.beta);
  } else if (type == "strategy") {
    return std::pow(t / (t + 1), config_.gamma);
  }
  return 1.0;
}

int CFRPlusSolver::GetValidActions(int street_idx, bool can_check, double pot_size,
                                   std::vector<std::pair<CFRAction, double>>& actions_out) const {
  actions_out.clear();
  actions_out.emplace_back(CFRAction::FOLD, 0);
  if (can_check) actions_out.emplace_back(CFRAction::CHECK, 0);
  actions_out.emplace_back(CFRAction::CALL, to_call_);
  actions_out.emplace_back(CFRAction::BET_HALF, pot_size * 0.5);
  actions_out.emplace_back(CFRAction::BET_POT, pot_size);
  actions_out.emplace_back(CFRAction::ALL_IN, pot_size * 2.0);
  return static_cast<int>(actions_out.size());
}

double CFRPlusSolver::ComputeEquity(int /*street_idx*/, int n_samples) {
  uint8_t board5[5] = {0};
  for (size_t i = 0; i < board_.size() && i < 5; i++) board5[i] = board_[i].Id();
  int bs = static_cast<int>(board_.size());
  if (hero_range_.NonZeroCount() == 0 || villain_range_.NonZeroCount() == 0) return 0.5;
  std::mt19937 rng(rng_());
  auto res = EquityCalculator::CalculateMonteCarlo(hero_range_, villain_range_, board5, bs,
                                                   n_samples, rng);
  return res.equity[0];
}

double CFRPlusSolver::CFR_External(int player, double reach_hero, double reach_villain,
                                   int street_idx, const std::string& history, int sample_count) {
  if (history.size() > 8) return 0;
  if (std::min(reach_hero, reach_villain) < 1e-8) return 0;

  bool is_terminal = false;
  if (history.size() >= 2) {
    char last = history.back();
    char prev = history[history.size() - 2];
    if (last == 'd' || (last == 'd' && prev == 'f')) is_terminal = true;
  }
  if (is_terminal || (history.size() >= 4 && history.find('f') != std::string::npos)) {
    double equity = ComputeEquity(street_idx, sample_count);
    double pot = pot_ + history.size() * 2.0;
    if (player == 0)
      return equity * pot - (1.0 - equity) * (pot * 0.5);
    else
      return (1.0 - equity) * pot - equity * (pot * 0.5);
  }

  CFRInfoSetKey key;
  key.street = street_idx;
  key.history = history;
  auto& node = node_map_[key];
  node.visit_count++;

  double strategy[NUM_CFR_ACTIONS];
  node.GetRegretMatchedStrategy(strategy);
  std::vector<std::pair<CFRAction, double>> valid_actions;
  bool can_check = (history.empty() || history.back() == 'k' || history.back() == 'K');
  double current_pot = pot_ + history.size() * 2.0;
  int num_actions =
      std::min(NUM_CFR_ACTIONS, GetValidActions(street_idx, can_check, current_pot, valid_actions));

  double util[NUM_CFR_ACTIONS] = {0};
  double node_util = 0;
  double equity = ComputeEquity(street_idx, sample_count);
  for (int a = 0; a < num_actions; a++) {
    CFRAction action = static_cast<CFRAction>(a);
    double action_cost = valid_actions[a].second;
    if (action == CFRAction::FOLD)
      util[a] = -0.5 * pot_;
    else if (action == CFRAction::CHECK || action == CFRAction::CALL)
      util[a] = equity * current_pot - (1.0 - equity) * action_cost;
    else
      util[a] = equity * (current_pot + action_cost) - (1.0 - equity) * action_cost;
    node_util += strategy[a] * util[a];
  }

  double d_regret = getDiscount(config_.iterations, "regret_plus");
  double d_avg = getDiscount(config_.iterations, "avg_strategy");
  for (int a = 0; a < num_actions; a++)
    node.cumulative_regret[a] += (util[a] - node_util) * reach_villain * d_regret;
  bool is_hero = (player == 0);
  for (int a = 0; a < num_actions; a++)
    node.strategy_sum[a] += (is_hero ? reach_hero : 1.0) * strategy[a] * d_avg;

  double total_util = 0;
  for (int a = 0; a < num_actions; a++) {
    CFRAction action = static_cast<CFRAction>(a);
    if (action == CFRAction::FOLD) {
      total_util += strategy[a] * util[a];
      continue;
    }
    std::string new_hist = history + CFRActionName[a][0];
    double nrh = reach_hero, nrv = reach_villain;
    if (player == 0)
      nrh *= strategy[a];
    else
      nrv *= strategy[a];
    int new_street = street_idx;
    if (action == CFRAction::CALL) new_street = std::min(street_idx + 1, 3);
    total_util += strategy[a] * CFR_External(player, nrh, nrv, new_street, new_hist, sample_count);
  }
  return total_util;
}

CFRPlusResult CFRPlusSolver::Solve() {
  auto start_time = std::chrono::high_resolution_clock::now();
  CFRPlusResult result;
  result.iterations = config_.iterations;
  result.mode_used = config_.mode;
  if (hero_range_.NonZeroCount() == 0) hero_range_ = Range::FullCombinatorial();
  if (villain_range_.NonZeroCount() == 0) villain_range_ = Range::FullCombinatorial();
  if (pot_ == 0) pot_ = 20;
  if (to_call_ == 0) to_call_ = 5;
  node_map_.clear();
  result.ev_history.clear();

  for (int iter = 0; iter < config_.iterations; iter++) {
    for (int p = 0; p < 2; p++) CFR_External(p, 1.0, 1.0, 0, "", config_.mc_samples);
    if (config_.mode == CFRMode::DISCOUNTED && iter > 50)
      for (auto& [key, node] : node_map_)
        node.ApplyDiscount(config_.alpha, config_.beta, config_.gamma);
    double total_regret = 0;
    int node_count = 0;
    for (const auto& [k, n] : node_map_) {
      for (int a = 0; a < NUM_CFR_ACTIONS; a++) total_regret += std::abs(n.cumulative_regret[a]);
      node_count++;
    }
    result.ev_history.push_back(node_count > 0 ? total_regret / node_count : 0);
    if (config_.verbose && (iter + 1) % 200 == 0)
      std::cout << "\rCFR+ iter " << (iter + 1) << "/" << config_.iterations
                << " | nodes=" << node_map_.size() << " | avg_regret=" << std::scientific
                << total_regret / std::max(1, node_count) << std::defaultfloat << std::flush;
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  result.time_seconds = std::chrono::duration<double>(end_time - start_time).count();
  for (const auto& [key, node] : node_map_) {
    result.strategy_profile[key] = {};
    node.GetAverageStrategy(result.strategy_profile[key].data());
  }
  result.exploitability = ComputeExploitability(result);
  return result;
}

std::array<double, NUM_CFR_ACTIONS> CFRPlusSolver::GetStrategy(const CFRInfoSetKey& key) const {
  auto it = node_map_.find(key);
  if (it == node_map_.end()) {
    std::array<double, NUM_CFR_ACTIONS> u;
    u.fill(1.0 / NUM_CFR_ACTIONS);
    return u;
  }
  std::array<double, NUM_CFR_ACTIONS> out;
  it->second.GetAverageStrategy(out.data());
  return out;
}

double CFRPlusSolver::ComputeExploitability(const CFRPlusResult& result) {
  if (result.strategy_profile.empty()) return 0;
  double total_regret_sq = 0;
  int count = 0;
  for (const auto& [key, node] : node_map_) {
    double avg[NUM_CFR_ACTIONS];
    node.GetAverageStrategy(avg);
    for (int a = 0; a < NUM_CFR_ACTIONS; a++) {
      double ir = node.cumulative_regret[a] / std::max(1, node.visit_count);
      total_regret_sq += ir * ir;
      count++;
    }
  }
  return count > 0 ? std::sqrt(total_regret_sq / count) * 1000 : 0;
}

std::string CFRPlusResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(4);
  const char* mode_names[] = {"Vanilla CFR", "Chance-Sampled CFR", "Discounted CFR (DCFR)"};
  oss << "=== CFR+ Solver (" << mode_names[static_cast<int>(mode_used)] << ") ===\n";
  oss << "Iterations: " << iterations << "\nTime: " << time_seconds
      << "s\nExploitability: " << exploitability << " mBB\nInfoSets: " << strategy_profile.size()
      << "\n";
  oss << "\n--- EV History ---\n" << EVHistoryString() << "\n--- Strategy Table ---\n";
  for (const auto& [key, strat] : strategy_profile) {
    oss << "S" << key.street << " [" << key.history << "]: ";
    for (int a = 0; a < NUM_CFR_ACTIONS; a++)
      if (strat[a] > 0.005) oss << CFRActionName[a] << ":" << int(strat[a] * 100 + 0.5) << "% ";
    oss << "\n";
  }
  return oss.str();
}

std::string CFRPlusResult::EVHistoryString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  int step = std::max(1, static_cast<int>(ev_history.size()) / 20);
  for (size_t i = 0; i < ev_history.size(); i += step)
    oss << "[" << std::setw(6) << i << "] " << ev_history[i] << "\n";
  return oss.str();
}

}  // namespace phase7
}  // namespace poker_engine
