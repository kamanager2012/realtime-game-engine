#pragma once

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "types.h"

namespace poker_engine::cfr {

struct CFRNode {
  static constexpr int kMaxActions = static_cast<int>(Action::AllIn) + 1;

  double regret_sum[kMaxActions];
  double strategy_sum[kMaxActions];
  double current_strategy[kMaxActions];
  int64_t times_visited = 0;

  CFRNode() {
    std::memset(regret_sum, 0, sizeof(regret_sum));
    std::memset(strategy_sum, 0, sizeof(strategy_sum));
    std::memset(current_strategy, 0, sizeof(current_strategy));

    int valid = valid_actions();
    for (int i = 0; i < kMaxActions; ++i) {
      current_strategy[i] = is_valid(static_cast<Action>(i)) ? 1.0 / valid : 0.0;
    }
  }

  static constexpr bool is_valid(Action) { return true; }

  int valid_actions() const {
    int count = 0;
    for (int i = 0; i < kMaxActions; ++i) {
      if (current_strategy[i] >= 0) count++;
    }
    return count > 0 ? count : 1;
  }

  void accumulate_regret(int action_idx, double regret, double floor, double ceil) {
    double new_val = regret_sum[action_idx] + regret;
    regret_sum[action_idx] = std::max(floor, std::min(ceil, new_val));
  }

  void accumulate_strategy(int action_idx, double weight) { strategy_sum[action_idx] += weight; }

  void compute_strategy() {
    double pos_regret_sum = 0.0;

    for (int a = 0; a < kMaxActions; ++a) {
      double r = regret_sum[a];
      if (r > 0) pos_regret_sum += r;
    }

    for (int a = 0; a < kMaxActions; ++a) {
      if (pos_regret_sum > 0 && regret_sum[a] > 0) {
        current_strategy[a] = regret_sum[a] / pos_regret_sum;
      } else {
        current_strategy[a] = 1.0 / kMaxActions;
      }
    }
  }

  void get_average_strategy(double out[kMaxActions]) const {
    double total = 0.0;
    for (int a = 0; a < kMaxActions; ++a) {
      total += std::max(0.0, strategy_sum[a]);
    }

    for (int a = 0; a < kMaxActions; ++a) {
      if (total > 0 && strategy_sum[a] > 0) {
        out[a] = strategy_sum[a] / total;
      } else {
        out[a] = 1.0 / kMaxActions;
      }
    }
  }

  std::vector<float> get_normalized_strategy() const {
    double strat[kMaxActions];
    get_average_strategy(strat);
    return std::vector<float>(strat, strat + kMaxActions);
  }

  void print() const {
    double avg[kMaxActions];
    get_average_strategy(avg);

    printf("  Node stats (visited=%lld):\n", (long long)times_visited);
    for (int a = 0; a < kMaxActions; ++a) {
      printf("    %s: regret=%.2f avg_strat=%.3f\n", action_name(static_cast<Action>(a)),
             regret_sum[a], avg[a]);
    }
  }
};

static_assert(sizeof(CFRNode) < 256, "CFRNode should be small for cache performance");

}  // namespace poker_engine::cfr
