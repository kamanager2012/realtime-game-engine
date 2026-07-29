#include "poker_engine/phase8/mc_cfr_deep.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"

namespace poker_engine {
namespace phase8 {
using namespace poker_engine::range;
using namespace poker_engine::evaluator;

MCNode::MCNode() : cumulative_utility(0), visit_count(0), reach_pr_sum(0) {
  std::fill(cumulative_regret, cumulative_regret + NUM_MC_ACTIONS, 0.0);
  std::fill(strategy_sum, strategy_sum + NUM_MC_ACTIONS, 0.0);
}

void MCNode::GetStrategy(double out[NUM_MC_ACTIONS]) const { GetRegretMatchedStrategy(out); }

void MCNode::GetAverageStrategy(double out[NUM_MC_ACTIONS]) const {
  double sum = 0;
  for (int a = 0; a < NUM_MC_ACTIONS; a++) {
    out[a] = strategy_sum[a];
    sum += out[a];
  }
  if (sum > 1e-10) {
    for (int a = 0; a < NUM_MC_ACTIONS; a++) out[a] /= sum;
  } else {
    for (int a = 0; a < NUM_MC_ACTIONS; a++) out[a] = 1.0 / NUM_MC_ACTIONS;
  }
}

void MCNode::GetRegretMatchedStrategy(double out[NUM_MC_ACTIONS]) const {
  double sum = 0;
  for (int a = 0; a < NUM_MC_ACTIONS; a++) {
    out[a] = (cumulative_regret[a] > 0) ? cumulative_regret[a] : 0;
    sum += out[a];
  }
  if (sum > 1e-10) {
    for (int a = 0; a < NUM_MC_ACTIONS; a++) out[a] /= sum;
  } else {
    for (int a = 0; a < NUM_MC_ACTIONS; a++) out[a] = 1.0 / NUM_MC_ACTIONS;
  }
}

std::string MCNode::BestAction() const {
  double avg[NUM_MC_ACTIONS];
  GetAverageStrategy(avg);
  int best = 0;
  for (int a = 1; a < NUM_MC_ACTIONS; a++)
    if (avg[a] > avg[best]) best = a;
  return MCActionName[best];
}

double MCNode::GetAverageEV() const {
  return visit_count == 0 ? 0 : cumulative_utility / visit_count;
}

void MCNode::ApplyRegretPruning(double threshold) {
  for (int a = 0; a < NUM_MC_ACTIONS; a++)
    if (cumulative_regret[a] < threshold) cumulative_regret[a] = threshold;
}

MCCRDeepSolver::MCCRDeepSolver(const MCConfig& config) : config_(config) {}
void MCCRDeepSolver::SetHeroRange(const Range& r) { hero_range_ = r; }
void MCCRDeepSolver::SetVillainRange(const Range& r) { villain_range_ = r; }
void MCCRDeepSolver::SetPot(double pot, double to_call) {
  pot_ = pot;
  to_call_ = to_call;
}

void MCCRDeepSolver::SetBoard(const std::vector<poker_engine::Card>& board) {
  board_.clear();
  for (const auto& c : board) board_.push_back(c);
}

void MCCRDeepSolver::SetBlueprint(
    const std::map<std::string, std::array<double, NUM_MC_ACTIONS>>& bp) {
  blueprint_ = bp;
}

double MCCRDeepSolver::ComputeEquity(int n_samples) {
  uint8_t board5[5] = {0};
  for (size_t i = 0; i < board_.size() && i < 5; i++) board5[i] = board_[i].Id();
  int bs = static_cast<int>(board_.size());
  if (hero_range_.NonZeroCount() == 0 || villain_range_.NonZeroCount() == 0) return 0.5;
  std::mt19937 rng(rng_());
  auto res = poker_engine::equity::EquityCalculator::CalculateMonteCarlo(
      hero_range_, villain_range_, board5, bs, n_samples, rng);
  return res.equity[0];
}

int MCCRDeepSolver::GetValidActions(int street_idx, double pot_size,
                                    std::vector<std::pair<MCCAction, double>>& actions_out) const {
  actions_out.clear();
  double eff_pot = pot_size;
  actions_out.emplace_back(MCCAction::FOLD, 0);
  if (to_call_ == 0 || street_idx == 0) actions_out.emplace_back(MCCAction::CHECK, 0);
  actions_out.emplace_back(MCCAction::CALL, to_call_);
  actions_out.emplace_back(MCCAction::BET_1_3POT, eff_pot * 0.33);
  actions_out.emplace_back(MCCAction::BET_1_2POT, eff_pot * 0.5);
  actions_out.emplace_back(MCCAction::BET_2_3POT, eff_pot * 0.67);
  actions_out.emplace_back(MCCAction::BET_POT, eff_pot);
  actions_out.emplace_back(MCCAction::ALL_IN, eff_pot * 2.0);
  return static_cast<int>(actions_out.size());
}

double MCCRDeepSolver::GetDiscount(int iter, const std::string& type) const {
  if (!config_.use_chance_sampling) return 1.0;
  double n = static_cast<double>(iter + 1);
  if (type == "regret_plus") return std::pow(n / (n + 1), config_.discount_factor);
  if (type == "strategy") return std::pow(n / (n + 1), 2.0);
  return 1.0;
}

std::string MCCRDeepSolver::SampleAction(const double strategy[NUM_MC_ACTIONS], std::mt19937& rng) {
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  double roll = dist(rng), cum = 0;
  for (int a = 0; a < NUM_MC_ACTIONS; a++) {
    cum += strategy[a];
    if (roll <= cum) return MCActionName[a];
  }
  return MCActionName[NUM_MC_ACTIONS - 1];
}

double MCCRDeepSolver::MCCR_External(int player, double reach_hero, double reach_villain,
                                     int street_idx, const std::string& history, int depth) {
  if (depth > config_.max_depth) return 0;
  double min_reach = std::min(reach_hero, reach_villain);
  if (min_reach < 1e-10) return 0;

  if (history.length() >= 2 && history.back() == 'f')
    return (player == 0) ? -pot_ * 0.3 : pot_ * 0.3;

  if (history.length() >= 6 || (depth >= 4 && history.length() >= 4)) {
    double equity = ComputeEquity(config_.mc_samples_per_iter);
    double showdown_ev = equity * pot_ - (1.0 - equity) * to_call_;
    return (player == 0) ? showdown_ev : -showdown_ev;
  }

  MCInfoSetKey key;
  key.street = street_idx;
  key.history = history;
  key.pot = pot_;
  key.to_call = to_call_;
  key.depth_bucket = depth / 3;

  bool is_hero_turn = history.empty() || (history.length() % 2 == 0);
  if (!is_hero_turn && !blueprint_.empty()) {
    auto bp_it = blueprint_.find(key.Key());
    if (bp_it != blueprint_.end()) {
      std::string opp_action = SampleAction(bp_it->second.data(), rng_);
      std::string new_hist = history + opp_action[0];
      return MCCR_External(player, reach_hero, reach_villain, street_idx, new_hist, depth + 1);
    }
  }

  auto& node = node_map_[key];
  node.visit_count++;

  double strategy[NUM_MC_ACTIONS];
  node.GetRegretMatchedStrategy(strategy);

  std::vector<std::pair<MCCAction, double>> valid_actions;
  int num_actions = GetValidActions(street_idx, pot_, valid_actions);

  double equity = ComputeEquity(config_.mc_samples_per_iter / 10);
  double util[NUM_MC_ACTIONS] = {0};
  double node_util = 0;

  for (int a = 0; a < num_actions; a++) {
    MCCAction action = static_cast<MCCAction>(a);
    double cost = valid_actions[a].second;
    if (action == MCCAction::FOLD)
      util[a] = -(to_call_ * 0.5);
    else if (action == MCCAction::CHECK || action == MCCAction::CALL)
      util[a] = equity * pot_ - (1.0 - equity) * cost;
    else
      util[a] = equity * (pot_ + cost) - (1.0 - equity) * cost;
    node_util += strategy[a] * util[a];
  }

  if (is_hero_turn) {
    double d = GetDiscount(config_.current_iteration, "regret_plus");
    for (int a = 0; a < num_actions; a++)
      node.cumulative_regret[a] += (util[a] - node_util) * reach_villain * d;
  }

  double d_avg = GetDiscount(config_.current_iteration, "strategy");
  for (int a = 0; a < num_actions; a++)
    node.strategy_sum[a] += strategy[a] * (is_hero_turn ? reach_hero : 1.0) * d_avg;

  double total_util = 0;
  if (is_hero_turn) {
    for (int a = 0; a < num_actions; a++) {
      std::string new_hist = history + MCActionName[a][0];
      total_util += strategy[a] * MCCR_External(player, reach_hero * strategy[a], reach_villain,
                                                street_idx, new_hist, depth + 1);
    }
  } else {
    double opp_strategy[NUM_MC_ACTIONS];
    double total_weight = 0;
    for (int a = 0; a < num_actions; a++) {
      opp_strategy[a] = 1.0;
      total_weight += opp_strategy[a];
    }
    for (int a = 0; a < num_actions; a++) opp_strategy[a] /= total_weight;

    int sampled_a = 0;
    std::uniform_real_distribution<double> dist(0, total_weight);
    double roll = dist(rng_), cum = 0;
    for (int a = 0; a < num_actions; a++) {
      cum += opp_strategy[a];
      if (roll <= cum) {
        sampled_a = a;
        break;
      }
    }

    std::string new_hist = history + MCActionName[sampled_a][0];
    total_util = opp_strategy[sampled_a] * MCCR_External(player, reach_hero,
                                                         reach_villain * opp_strategy[sampled_a],
                                                         street_idx, new_hist, depth + 1);
  }

  node.cumulative_utility += total_util;
  total_mc_samples_++;

  if (config_.prune_threshold > 0 && (node.visit_count % 100 == 0))
    node.ApplyRegretPruning(config_.prune_threshold);

  return total_util;
}

double MCCRDeepSolver::MCCR_Outcome(int player, double reach_hero, double reach_villain,
                                    int street_idx, const std::string& history, int depth,
                                    double sample_reach) {
  sample_reach = std::max(sample_reach, 1e-10);
  if (depth > config_.max_depth) return 0;
  if (history.length() >= 6 || depth >= 4) {
    double equity = ComputeEquity(1);
    double pot_final = pot_ + depth * 2.0;
    double hero_ev = equity * pot_final - (1.0 - equity) * to_call_;
    return (player == 0) ? hero_ev / sample_reach : -hero_ev / sample_reach;
  }
  return MCCR_External(player, reach_hero, reach_villain, street_idx, history, depth);
}

MCCRResult MCCRDeepSolver::Solve() {
  auto start_time = std::chrono::high_resolution_clock::now();
  MCCRResult result;
  result.total_iterations = config_.iterations;

  if (hero_range_.NonZeroCount() == 0) hero_range_ = Range::FullCombinatorial();
  if (villain_range_.NonZeroCount() == 0) villain_range_ = Range::FullCombinatorial();
  if (pot_ == 0) {
    pot_ = 20;
    to_call_ = 5;
  }

  node_map_.clear();
  result.ev_per_iteration.clear();
  total_mc_samples_ = 0;

  for (int iter = 0; iter < config_.iterations; iter++) {
    config_.current_iteration = iter + 1;
    for (int p = 0; p < 2; p++) {
      if (config_.mode == MCRMode::OUTCOME_SAMPLING)
        MCCR_Outcome(p, 1.0, 1.0, 0, "", 0, 1.0);
      else
        MCCR_External(p, 1.0, 1.0, 0, "", 0);
    }

    if ((iter + 1) % 10 == 0) {
      double total_regret = 0;
      for (const auto& [k, n] : node_map_)
        for (int a = 0; a < NUM_MC_ACTIONS; a++) total_regret += std::abs(n.cumulative_regret[a]);
      result.ev_per_iteration.push_back(node_map_.size() > 0 ? total_regret / node_map_.size() : 0);
    }

    if (config_.verbose && (iter + 1) % 500 == 0)
      std::cout << "\rMC-CFR-Deep iter " << (iter + 1) << "/" << config_.iterations
                << " | nodes=" << node_map_.size() << std::flush;
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  result.total_time_seconds = std::chrono::duration<double>(end_time - start_time).count();

  for (const auto& [key, node] : node_map_) {
    MCResultEntry entry;
    entry.visits = node.visit_count;
    node.GetAverageStrategy(entry.avg_strategy.data());
    entry.avg_ev = node.GetAverageEV();
    entry.best_action = node.BestAction();
    result.strategy_map[key] = entry;
  }

  double total_regret = 0;
  int count = 0;
  for (const auto& [k, n] : node_map_)
    for (int a = 0; a < NUM_MC_ACTIONS; a++) {
      total_regret += std::abs(n.cumulative_regret[a]);
      count++;
    }
  result.exploitability_mbb = count > 0 ? (total_regret / count) * 1000 : 0;

  return result;
}

std::array<double, NUM_MC_ACTIONS> MCCRDeepSolver::GetStrategy(const MCInfoSetKey& key) const {
  auto it = node_map_.find(key);
  if (it == node_map_.end()) {
    std::array<double, NUM_MC_ACTIONS> uniform;
    uniform.fill(1.0 / NUM_MC_ACTIONS);
    return uniform;
  }
  std::array<double, NUM_MC_ACTIONS> out;
  it->second.GetAverageStrategy(out.data());
  return out;
}

std::string MCCRResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(4);
  oss << "=== MC-CFR Deep Results ===\n";
  oss << "Iterations: " << total_iterations << "\n";
  oss << "Time: " << total_time_seconds << "s\n";
  oss << "InfoSets: " << strategy_map.size() << "\n";
  oss << "Exploitability: " << exploitability_mbb << " mBB\n";

  oss << "\n--- Key Strategies ---\n";
  int shown = 0;
  for (const auto& [key, entry] : strategy_map) {
    if (shown >= 10) break;
    oss << "S" << key.street << " [" << key.history << "] pot=" << int(key.pot) << "\n  ";
    for (int a = 0; a < NUM_MC_ACTIONS; a++)
      if (entry.avg_strategy[a] > 0.01)
        oss << MCActionName[a] << ":" << int(entry.avg_strategy[a] * 100 + 0.5) << "% ";
    oss << "| Best: " << entry.best_action << "\n";
    shown++;
  }

  if (!ev_per_iteration.empty()) {
    oss << "\n--- Convergence ---\n";
    int step = std::max(1, static_cast<int>(ev_per_iteration.size()) / 10);
    for (size_t i = 0; i < ev_per_iteration.size(); i += step)
      oss << "  [" << i * 10 << "] " << ev_per_iteration[i] << "\n";
  }
  return oss.str();
}

}  // namespace phase8
}  // namespace poker_engine
