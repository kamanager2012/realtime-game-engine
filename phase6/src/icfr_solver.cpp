#include "poker_engine/phase6/icfr_solver.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"

namespace poker_engine {
namespace phase6 {
using namespace poker_engine::range;
using namespace poker_engine::equity;
using namespace poker_engine::evaluator;

// ===================== ICNode =====================
ICNode::ICNode() {
  std::fill(cumulative_regret, cumulative_regret + NUM_IC_ACTIONS, 0.0);
  std::fill(strategy_sum, strategy_sum + NUM_IC_ACTIONS, 0.0);
}

void ICNode::GetStrategy(double out[NUM_IC_ACTIONS]) const {
  double sum = 0;
  for (int a = 0; a < NUM_IC_ACTIONS; a++) {
    out[a] = (cumulative_regret[a] > 0) ? cumulative_regret[a] : 0;
    sum += out[a];
  }
  if (sum > 1e-10) {
    for (int a = 0; a < NUM_IC_ACTIONS; a++) out[a] /= sum;
  } else {
    for (int a = 0; a < NUM_IC_ACTIONS; a++) out[a] = 1.0 / NUM_IC_ACTIONS;
  }
}

void ICNode::GetAverageStrategy(double out[NUM_IC_ACTIONS]) const {
  double sum = 0;
  for (int a = 0; a < NUM_IC_ACTIONS; a++) {
    out[a] = strategy_sum[a];
    sum += out[a];
  }
  if (sum > 1e-10) {
    for (int a = 0; a < NUM_IC_ACTIONS; a++) out[a] /= sum;
  } else {
    for (int a = 0; a < NUM_IC_ACTIONS; a++) out[a] = 1.0 / NUM_IC_ACTIONS;
  }
}

std::string ICNode::BestAction() const {
  double avg[NUM_IC_ACTIONS];
  GetAverageStrategy(avg);
  int best = 0;
  for (int a = 1; a < NUM_IC_ACTIONS; a++) {
    if (avg[a] > avg[best]) best = a;
  }
  return ICActionName[best];
}

// ===================== ICFRSolver =====================
ICFRSolver::ICFRSolver(const ICFRConfig& config) : config_(config) {}

void ICFRSolver::SetHeroRange(const Range& r) { hero_range_ = r; }
void ICFRSolver::SetVillainRange(const Range& r) { villain_range_ = r; }

void ICFRSolver::SetOpponentBlueprint(const BlueprintStrategy& bp) { blueprint_ = bp; }

void ICFRSolver::SetBoard(const std::vector<Card>& board) {
  board_.clear();
  for (const auto& c : board)
    if (c.Id() != 0) board_.push_back(c);
}

void ICFRSolver::LearnBlueprintFromHistory(
    const std::vector<poker_engine::phase4::HandHistory>& hands, const std::string& villain_name) {
  blueprint_.clear();

  for (const auto& hh : hands) {
    for (const auto& sr : hh.streets) {
      for (const auto& action : sr.actions) {
        if (action.player_name != villain_name) continue;

        ICInfoSetKey key;
        key.street = static_cast<int>(sr.street);
        key.pot = hh.total_pot;

        std::string hist;
        for (const auto& a : sr.actions) {
          switch (a.action) {
            case poker_engine::phase4::ActionType::FOLD:
              hist += "f";
              break;
            case poker_engine::phase4::ActionType::CHECK:
              hist += "k";
              break;
            case poker_engine::phase4::ActionType::CALL:
              hist += "c";
              break;
            case poker_engine::phase4::ActionType::BET:
              hist += "b";
              break;
            case poker_engine::phase4::ActionType::RAISE:
              hist += "r";
              break;
            case poker_engine::phase4::ActionType::ALL_IN:
              hist += "a";
              break;
            case poker_engine::phase4::ActionType::POST_SB:
              hist += "s";
              break;
            case poker_engine::phase4::ActionType::POST_BB:
              hist += "B";
              break;
            default:
              hist += "x";
              break;
          }
        }
        key.history = hist;

        int action_idx = 0;
        switch (action.action) {
          case poker_engine::phase4::ActionType::FOLD:
            action_idx = 0;
            break;
          case poker_engine::phase4::ActionType::CHECK:
            action_idx = 1;
            break;
          case poker_engine::phase4::ActionType::CALL:
            action_idx = 2;
            break;
          case poker_engine::phase4::ActionType::BET:
            action_idx = 4;
            break;
          case poker_engine::phase4::ActionType::RAISE:
            action_idx = 3;
            break;
          case poker_engine::phase4::ActionType::ALL_IN:
            action_idx = 5;
            break;
          default:
            action_idx = 1;
            break;
        }

        blueprint_[key.Key()][action_idx] += 1.0;
      }
    }
  }

  for (auto& [key, arr] : blueprint_) {
    double total = 0;
    for (int a = 0; a < NUM_IC_ACTIONS; a++) total += arr[a];
    if (total > 0) {
      for (int a = 0; a < NUM_IC_ACTIONS; a++) arr[a] /= total;
    } else {
      for (int a = 0; a < NUM_IC_ACTIONS; a++) arr[a] = 1.0 / NUM_IC_ACTIONS;
    }

    if (config_.blueprint_weight < 1.0) {
      double mix = config_.blueprint_weight;
      for (int a = 0; a < NUM_IC_ACTIONS; a++)
        arr[a] = mix * arr[a] + (1.0 - mix) * (1.0 / NUM_IC_ACTIONS);
    }
  }
}

double ICFRSolver::ComputeEquity(int player, int street_idx) {
  // Lazy init
  if (hero_range_.NonZeroCount() == 0) hero_range_ = Range::FullCombinatorial();
  if (villain_range_.NonZeroCount() == 0) villain_range_ = Range::FullCombinatorial();

  uint8_t board5[5] = {0};
  for (size_t i = 0; i < board_.size() && i < 5; i++) board5[i] = board_[i].Id();
  int bs = static_cast<int>(board_.size());

  auto res = EquityCalculator::CalculateMonteCarlo(hero_range_, villain_range_, board5, bs,
                                                   config_.mc_samples, rng_);

  return res.equity[0];
}

double ICFRSolver::ICFR(int hero_player, double reach_hero, double reach_villain, int street_idx,
                        const std::string& history) {
  if (history.size() > 6) return 0;  // depth limit

  double min_reach = std::min(reach_hero, reach_villain);
  if (min_reach < 1e-6) return 0;

  ICInfoSetKey key;
  key.street = street_idx;
  key.history = history;
  key.pot = 15.0 + history.size() * 5.0;
  key.to_call = history.size() > 0 ? 5.0 * history.size() : 0;

  auto& node = node_map_[key];
  node.visit_count++;

  double hero_strategy[NUM_IC_ACTIONS];
  node.GetStrategy(hero_strategy);

  double opp_strategy[NUM_IC_ACTIONS];
  auto bp_it = blueprint_.find(key.Key());
  if (bp_it != blueprint_.end()) {
    for (int a = 0; a < NUM_IC_ACTIONS; a++) opp_strategy[a] = bp_it->second[a];
  } else {
    for (int a = 0; a < NUM_IC_ACTIONS; a++) opp_strategy[a] = 1.0 / NUM_IC_ACTIONS;
  }

  bool is_hero_turn = (street_idx == 0) || (history.size() % 2 == 0);

  double equity = ComputeEquity(0, street_idx);
  double util[NUM_IC_ACTIONS] = {0};
  double node_util = 0;

  for (int a = 0; a < NUM_IC_ACTIONS; a++) {
    double action_prob = is_hero_turn ? hero_strategy[a] : opp_strategy[a];

    if (action_prob < 1e-8) {
      util[a] = 0;
      continue;
    }

    if (static_cast<ICAction>(a) == ICAction::FOLD) {
      util[a] = -key.to_call * 0.5;
    } else if (static_cast<ICAction>(a) == ICAction::CHECK ||
               static_cast<ICAction>(a) == ICAction::CALL) {
      util[a] = equity * key.pot - (1.0 - equity) * key.to_call;
    } else if (static_cast<ICAction>(a) == ICAction::BET_50) {
      double bet = key.pot * 0.5;
      util[a] = equity * (key.pot + bet) - (1.0 - equity) * bet;
    } else if (static_cast<ICAction>(a) == ICAction::BET_POT) {
      util[a] = equity * (key.pot + key.pot) - (1.0 - equity) * key.pot;
    } else if (static_cast<ICAction>(a) == ICAction::ALL_IN) {
      double allin = key.pot * 2.0;
      util[a] = equity * (key.pot + allin) - (1.0 - equity) * allin;
    }

    node_util += action_prob * util[a];
  }

  // Regret update (Hero only)
  if (is_hero_turn) {
    for (int a = 0; a < NUM_IC_ACTIONS; a++) {
      double regret = util[a] - node_util;
      node.cumulative_regret[a] += regret * reach_villain * config_.discount;
    }

    for (int a = 0; a < NUM_IC_ACTIONS; a++) {
      node.strategy_sum[a] += hero_strategy[a] * reach_hero;
    }
  }

  // Recurse
  double total_util = 0;
  for (int a = 0; a < NUM_IC_ACTIONS; a++) {
    double action_prob = is_hero_turn ? hero_strategy[a] : opp_strategy[a];
    if (action_prob < 1e-8) continue;

    if (static_cast<ICAction>(a) == ICAction::FOLD) {
      total_util += action_prob * util[a];
      continue;
    }

    double new_reach_hero = reach_hero;
    double new_reach_villain = reach_villain;
    if (is_hero_turn) {
      new_reach_hero *= action_prob;
    } else {
      new_reach_villain *= action_prob;
    }

    char action_char = ICActionName[a][0];
    std::string new_hist = history + action_char;
    int next_street = street_idx + (a == static_cast<int>(ICAction::CALL) ? 1 : 0);
    double child_util = ICFR(hero_player, new_reach_hero, new_reach_villain, next_street, new_hist);
    total_util += action_prob * child_util;
  }

  return total_util;
}

ICFRResult ICFRSolver::Solve() {
  ICFRResult result;
  result.iterations = config_.iterations;

  if (hero_range_.NonZeroCount() == 0) hero_range_ = Range::FullCombinatorial();
  if (villain_range_.NonZeroCount() == 0) villain_range_ = Range::FullCombinatorial();

  node_map_.clear();

  for (int iter = 0; iter < config_.iterations; iter++) {
    double reach[2] = {1.0, 1.0};
    double ev = ICFR(0, reach[0], reach[1], 0, "");
    result.achieved_ev += ev;

    if (config_.verbose && (iter + 1) % 200 == 0) {
      std::cout << "\rICFR iter " << (iter + 1) << "/" << config_.iterations << std::flush;
    }
  }

  if (config_.verbose && config_.iterations > 0) std::cout << std::endl;

  for (const auto& [key, node] : node_map_) {
    result.strategy_profile[key] = {};
    node.GetAverageStrategy(result.strategy_profile[key].data());
  }

  result.exploitability_vs_blueprint = ComputeExploitability(result);
  result.achieved_ev /= config_.iterations;

  return result;
}

double ICFRSolver::ComputeExploitability(const ICFRResult& result) {
  if (result.strategy_profile.empty()) return 0;

  double total_regret = 0;
  int count = 0;

  for (const auto& [key, node] : node_map_) {
    double avg[NUM_IC_ACTIONS];
    double sum = 0;
    for (int a = 0; a < NUM_IC_ACTIONS; a++) {
      avg[a] = node.strategy_sum[a];
      sum += avg[a];
    }
    if (sum > 1e-10)
      for (int a = 0; a < NUM_IC_ACTIONS; a++) avg[a] /= sum;
    else
      for (int a = 0; a < NUM_IC_ACTIONS; a++) avg[a] = 1.0 / NUM_IC_ACTIONS;

    double opp_strategy[NUM_IC_ACTIONS];
    auto bp_it = blueprint_.find(key.Key());
    for (int a = 0; a < NUM_IC_ACTIONS; a++) {
      opp_strategy[a] = (bp_it != blueprint_.end()) ? bp_it->second[a] : 1.0 / NUM_IC_ACTIONS;
    }

    for (int a = 0; a < NUM_IC_ACTIONS; a++) {
      double diff = avg[a] - opp_strategy[a];
      total_regret += diff * diff;
      count++;
    }
  }

  if (count == 0) return 0;
  return std::sqrt(total_regret / count) * 1000;  // in mBB
}

std::array<double, NUM_IC_ACTIONS> ICFRSolver::GetStrategy(const ICInfoSetKey& key) const {
  auto it = node_map_.find(key);
  if (it == node_map_.end()) {
    std::array<double, NUM_IC_ACTIONS> uniform;
    uniform.fill(1.0 / NUM_IC_ACTIONS);
    return uniform;
  }
  std::array<double, NUM_IC_ACTIONS> out;
  it->second.GetAverageStrategy(out.data());
  return out;
}

// ===================== ICFRResult display ====================

std::string ICFRResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3);
  oss << "=== Imitation CFR (Best Response) ===\n";
  oss << "Iterations: " << iterations << "\n";
  oss << "vs Blueprint exploitability: " << exploitability_vs_blueprint << " mBB\n";
  oss << "Achieved EV: " << achieved_ev << "\n";
  oss << "Nodes: " << strategy_profile.size() << "\n\n";

  for (const auto& [key, strat] : strategy_profile) {
    oss << "S" << key.street << " [" << key.history << "] ";
    oss << "pot=" << int(key.pot) << " to_call=" << int(key.to_call) << "\n  ";
    for (int a = 0; a < NUM_IC_ACTIONS; a++) {
      if (strat[a] > 0.005) oss << ICActionName[a] << ":" << int(strat[a] * 100 + 0.5) << "% ";
    }
    oss << "\n";
  }
  return oss.str();
}

}  // namespace phase6
}  // namespace poker_engine
