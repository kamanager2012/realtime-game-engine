#pragma once
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase8 {

struct AgentConfig {
  std::string name;
  std::string strategy;
  double starting_stack;
  enum Style { NIT, ROCK, TAG, LAG, MANIAC, CALLING_STATION, RANDOM } style;
  int aggression_level = 5;
};

struct SimAction {
  int agent_id;
  std::string action;
  double amount;
  double pot_after;
};

struct SimHandResult {
  int hand_id;
  std::vector<int> winners;
  std::vector<double> winnings;
  double pot_size;
  std::vector<SimAction> actions;
};

struct SimRoundResult {
  int round_num;
  std::vector<std::pair<int, double>> payouts;
};

struct SimOutcome {
  int total_hands;
  std::vector<std::pair<std::string, double>> final_equity;
  std::map<std::string, std::map<std::string, double>> matchup_matrix;
  std::vector<std::pair<std::string, int>> rankings;
  std::string ToString() const;
};

class MultiAgentSimulator {
 public:
  explicit MultiAgentSimulator(int num_hands_per_matchup = 500);

  void AddAgent(const AgentConfig& config);
  void AddAgent(const std::string& name, const std::string& strategy,
                AgentConfig::Style style = AgentConfig::TAG);

  SimOutcome RunRoundRobin();
  SimRoundResult RunMatchup(int agent_a, int agent_b, int num_hands);
  SimRoundResult RunFreeForAll(int num_hands);

  static double HeadToHeadEquity(const std::string& strategy_a, const std::string& strategy_b,
                                 int n_samples = 5000);

  void SetVerbose(bool v) { verbose_ = v; }
  int GetAgentsCount() const { return static_cast<int>(agents_.size()); }

 private:
  int num_hands_per_matchup_;
  std::vector<AgentConfig> agents_;
  std::mt19937 rng_{42};
  bool verbose_ = false;

  SimRoundResult SimulateMatchup(const AgentConfig& a, const AgentConfig& b, int num_hands,
                                 bool detailed = false);
};

}  // namespace phase8
}  // namespace poker_engine
