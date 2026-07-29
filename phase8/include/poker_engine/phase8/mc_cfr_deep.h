#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase8 {

enum class MCCAction : uint8_t {
  FOLD = 0,
  CHECK,
  CALL,
  BET_1_3POT,
  BET_1_2POT,
  BET_2_3POT,
  BET_POT,
  ALL_IN,
  _COUNT
};

static constexpr int NUM_MC_ACTIONS = static_cast<int>(MCCAction::_COUNT);
static constexpr const char* MCActionName[] = {"FOLD",    "CHECK",   "CALL",    "BET_33%",
                                               "BET_50%", "BET_67%", "BET_POT", "ALL_IN"};

struct MCInfoSetKey {
  int street;
  std::string history;
  double pot;
  double to_call;
  int depth_bucket;

  std::string Key() const {
    return std::to_string(street) + ":" + history + ":p" + std::to_string(static_cast<int>(pot)) +
           ":c" + std::to_string(static_cast<int>(to_call));
  }

  bool operator<(const MCInfoSetKey& o) const { return Key() < o.Key(); }
};

struct MCNode {
  double cumulative_regret[NUM_MC_ACTIONS];
  double strategy_sum[NUM_MC_ACTIONS];
  double cumulative_utility;
  int visit_count;
  double reach_pr_sum;

  MCNode();
  void GetStrategy(double out[NUM_MC_ACTIONS]) const;
  void GetAverageStrategy(double out[NUM_MC_ACTIONS]) const;
  void GetRegretMatchedStrategy(double out[NUM_MC_ACTIONS]) const;
  std::string BestAction() const;
  double GetAverageEV() const;
  void ApplyRegretPruning(double threshold);
};

struct MCResultEntry {
  std::array<double, NUM_MC_ACTIONS> avg_strategy;
  double avg_ev;
  int visits;
  std::string best_action;
};

struct MCCRResult {
  std::map<MCInfoSetKey, MCResultEntry> strategy_map;
  double exploitability_mbb = 0;
  int total_iterations = 0;
  double total_time_seconds = 0;
  std::vector<double> ev_per_iteration;
  std::string ToString() const;
};

enum class MCRMode { EXTERNAL_SAMPLING, OUTCOME_SAMPLING, CHANCE_SAMPLED_DFS };

struct MCConfig {
  int iterations = 5000;
  int mc_samples_per_iter = 50;
  MCRMode mode = MCRMode::EXTERNAL_SAMPLING;
  bool use_regret_matching = true;
  double discount_factor = 0.995;
  double prune_threshold = 0.001;
  bool use_chance_sampling = true;
  int max_depth = 12;
  double blueprint_weight = 0.0;
  bool verbose = false;

  // Mutable iteration counter (used internally during Solve)
  mutable int current_iteration = 0;
};

class MCCRDeepSolver {
 public:
  explicit MCCRDeepSolver(const MCConfig& config = MCConfig());

  void SetHeroRange(const poker_engine::range::Range& r);
  void SetVillainRange(const poker_engine::range::Range& r);
  void SetBoard(const std::vector<poker_engine::Card>& board);
  void SetPot(double pot, double to_call = 0);
  void SetBlueprint(const std::map<std::string, std::array<double, NUM_MC_ACTIONS>>& bp);

  MCCRResult Solve();
  std::array<double, NUM_MC_ACTIONS> GetStrategy(const MCInfoSetKey& key) const;

 private:
  MCConfig config_;
  poker_engine::range::Range hero_range_;
  poker_engine::range::Range villain_range_;
  std::vector<poker_engine::Card> board_;
  double pot_ = 0;
  double to_call_ = 0;
  std::map<MCInfoSetKey, MCNode> node_map_;
  std::map<std::string, std::array<double, NUM_MC_ACTIONS>> blueprint_;
  std::mt19937 rng_{42};
  int64_t total_mc_samples_ = 0;

  double MCCR_External(int player, double reach_hero, double reach_villain, int street_idx,
                       const std::string& history, int depth);
  double MCCR_Outcome(int player, double reach_hero, double reach_villain, int street_idx,
                      const std::string& history, int depth, double sample_reach);

  double ComputeEquity(int n_samples);
  int GetValidActions(int street_idx, double pot_size,
                      std::vector<std::pair<MCCAction, double>>& actions_out) const;
  double GetDiscount(int iter, const std::string& type) const;
  std::string SampleAction(const double strategy[NUM_MC_ACTIONS], std::mt19937& rng);
};

}  // namespace phase8
}  // namespace poker_engine
