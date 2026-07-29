#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace poker_engine::cfr {

struct CFRStoreNode {
  static constexpr int kMaxActions = 5;
  std::array<double, kMaxActions> regret_sum = {};
  std::array<double, kMaxActions> strategy_sum = {};
  std::array<double, kMaxActions> current_strategy = {};
  int64_t times_visited = 0;

  void AddRegret(int action, double regret) {
    if (action >= 0 && action < kMaxActions) {
      regret_sum[action] += regret;
    }
  }

  void SetStrategy(int action, double prob) {
    if (action >= 0 && action < kMaxActions) {
      current_strategy[action] = prob;
    }
  }

  void IncrementVisits() { times_visited++; }

  std::array<double, kMaxActions> GetAverageStrategy() const {
    std::array<double, kMaxActions> avg = {};
    double total = 0.0;
    for (int i = 0; i < kMaxActions; ++i) total += strategy_sum[i];
    if (total > 0.0) {
      for (int i = 0; i < kMaxActions; ++i) avg[i] = strategy_sum[i] / total;
    }
    return avg;
  }

  void GetAverageStrategy(float* out) const {
    auto avg = GetAverageStrategy();
    for (int i = 0; i < kMaxActions; ++i) out[i] = static_cast<float>(avg[i]);
  }
};

}  // namespace poker_engine::cfr
