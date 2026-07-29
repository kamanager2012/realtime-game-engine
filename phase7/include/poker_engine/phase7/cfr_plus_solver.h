#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase7 {

enum class CFRAction : uint8_t { FOLD = 0, CHECK, CALL, BET_HALF, BET_POT, ALL_IN, _COUNT };

static constexpr int NUM_CFR_ACTIONS = static_cast<int>(CFRAction::_COUNT);
static constexpr const char* CFRActionName[] = {"FOLD",    "CHECK",   "CALL",
                                                "BET_50%", "BET_POT", "ALL_IN"};

struct CFRInfoSetKey {
  int street;
  std::string history;
  bool operator<(const CFRInfoSetKey& o) const {
    if (street != o.street) return street < o.street;
    return history < o.history;
  }
};

enum class CFRMode { VANILLA, CHANCE_SAMPLED, DISCOUNTED };

struct CFRNode {
  double cumulative_regret[NUM_CFR_ACTIONS];
  double strategy_sum[NUM_CFR_ACTIONS];
  double positive_regret_sum[NUM_CFR_ACTIONS];
  int visit_count = 0;
  double reach_pr = 0;

  CFRNode();
  void GetRegretMatchedStrategy(double out[NUM_CFR_ACTIONS]) const;
  void GetAverageStrategy(double out[NUM_CFR_ACTIONS]) const;
  double GetAverageEV() const;
  std::string BestAction() const;
  void ApplyDiscount(double alpha, double beta, double gamma);
};

struct CFRPlusResult {
  std::map<CFRInfoSetKey, std::array<double, NUM_CFR_ACTIONS>> strategy_profile;
  double exploitability = 0;
  int iterations = 0;
  std::vector<double> ev_history;
  double time_seconds = 0;
  CFRMode mode_used = CFRMode::VANILLA;
  std::string ToString() const;
  std::string EVHistoryString() const;
};

struct CFRConfig {
  int iterations = 2000;
  int mc_samples = 1000;
  CFRMode mode = CFRMode::DISCOUNTED;
  double alpha = 1.5;
  double beta = 0.5;
  double gamma = 2.0;
  double prune_threshold = 0.0;
  bool verbose = false;
};

class CFRPlusSolver {
 public:
  explicit CFRPlusSolver(const CFRConfig& config = CFRConfig());
  void SetHeroRange(const poker_engine::range::Range& r);
  void SetVillainRange(const poker_engine::range::Range& r);
  void SetBoard(const std::vector<poker_engine::Card>& board);
  void SetPot(double pot);
  void SetToCall(double to_call);
  CFRPlusResult Solve();
  std::array<double, NUM_CFR_ACTIONS> GetStrategy(const CFRInfoSetKey& key) const;
  double ComputeExploitability(const CFRPlusResult& result);

 private:
  CFRConfig config_;
  poker_engine::range::Range hero_range_;
  poker_engine::range::Range villain_range_;
  std::vector<poker_engine::Card> board_;
  double pot_ = 0;
  double to_call_ = 0;
  std::map<CFRInfoSetKey, CFRNode> node_map_;
  std::mt19937 rng_{42};
  double getDiscount(int iter, const std::string& type) const;
  double CFR_External(int player, double reach_hero, double reach_villain, int street_idx,
                      const std::string& history, int sample_count);
  double ComputeEquity(int street_idx, int n_samples);
  int GetValidActions(int street_idx, bool can_check, double pot_size,
                      std::vector<std::pair<CFRAction, double>>& actions_out) const;
};

}  // namespace phase7
}  // namespace poker_engine
