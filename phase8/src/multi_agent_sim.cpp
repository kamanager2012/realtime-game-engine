#include "poker_engine/phase8/multi_agent_sim.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/range/hand_id.h"

namespace poker_engine {
namespace phase8 {
using namespace poker_engine::range;
using namespace poker_engine::evaluator;

std::string SimOutcome::ToString() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(2);
  o << "=== Multi-Agent Simulation Results ===\n";
  o << "Total hands per matchup: " << total_hands << "\n\n";
  o << "=== Rankings ===\n";
  for (size_t i = 0; i < rankings.size(); i++)
    o << "#" << (i + 1) << " " << rankings[i].first << ": $" << rankings[i].second << "\n";
  o << "\n=== Matchup Matrix ===\n";
  o << std::setw(16) << std::left << "";
  for (const auto& r : rankings) o << std::setw(10) << std::right << r.first.substr(0, 9);
  o << "\n";
  for (const auto& [a_name, row] : matchup_matrix) {
    o << std::setw(16) << std::left << a_name;
    for (const auto& [b_name, eq] : row) o << std::setw(10) << std::right << int(eq * 100) << "%";
    o << "\n";
  }
  o << "\n=== Final Equity ===\n";
  for (const auto& [name, eq] : final_equity)
    o << "  " << std::setw(16) << std::left << name << ": $" << eq << "\n";
  return o.str();
}

MultiAgentSimulator::MultiAgentSimulator(int n) : num_hands_per_matchup_(n) {}

void MultiAgentSimulator::AddAgent(const AgentConfig& c) { agents_.push_back(c); }
void MultiAgentSimulator::AddAgent(const std::string& name, const std::string& strategy,
                                   AgentConfig::Style style) {
  agents_.push_back({name, strategy, 10000, style, 5});
}

double MultiAgentSimulator::HeadToHeadEquity(const std::string& sa, const std::string& sb,
                                             int n_samples) {
  Range ra = Range::FromString(sa), rb = Range::FromString(sb);
  uint8_t board5[5] = {0};
  std::mt19937 rng(42);
  auto res = poker_engine::equity::EquityCalculator::CalculateMonteCarlo(ra, rb, board5, 0,
                                                                         n_samples, rng);
  return res.equity[0];
}

SimRoundResult MultiAgentSimulator::SimulateMatchup(const AgentConfig& a, const AgentConfig& b,
                                                    int num_hands, bool) {
  SimRoundResult result;
  double base_eq_a = HeadToHeadEquity(a.strategy, b.strategy, 5000);
  double mod = 0;
  if (a.style == AgentConfig::LAG || a.style == AgentConfig::MANIAC) mod += 0.02;
  if (b.style == AgentConfig::NIT || b.style == AgentConfig::ROCK) mod += 0.02;
  if (a.style == AgentConfig::NIT || a.style == AgentConfig::ROCK) mod -= 0.02;
  if (b.style == AgentConfig::LAG || b.style == AgentConfig::MANIAC) mod -= 0.02;

  double eq_a = std::clamp(base_eq_a + mod, 0.1, 0.9);
  double stack_a = a.starting_stack;
  double stack_b = b.starting_stack;

  std::uniform_real_distribution<double> variance(-0.05, 0.05);

  for (int h = 0; h < num_hands; h++) {
    double hand_eq = std::clamp(eq_a + variance(rng_), 0.1, 0.9);
    double pot = 3.0;
    double rake = pot * 0.05;
    double winner_pot = pot - rake;
    bool a_wins = (rng_() % 10000 < static_cast<int>(hand_eq * 10000));

    if (a_wins) {
      stack_a += winner_pot;
      stack_b -= winner_pot;
    } else {
      stack_b += winner_pot;
      stack_a -= winner_pot;
    }
  }

  result.round_num = 0;
  result.payouts = {{0, stack_a - a.starting_stack}, {1, stack_b - b.starting_stack}};
  return result;
}

SimRoundResult MultiAgentSimulator::RunMatchup(int ai, int bi, int nh) {
  if (ai < 0 || ai >= static_cast<int>(agents_.size()) || bi < 0 ||
      bi >= static_cast<int>(agents_.size()))
    return {};
  return SimulateMatchup(agents_[ai], agents_[bi], nh, false);
}

SimRoundResult MultiAgentSimulator::RunFreeForAll(int nh) {
  SimRoundResult result;
  if (agents_.size() < 3) return result;

  std::vector<double> stacks(agents_.size());
  for (size_t i = 0; i < agents_.size(); i++) stacks[i] = agents_[i].starting_stack;

  for (int h = 0; h < nh; h++) {
    double pot = agents_.size() * 2.5;
    double rake = pot * 0.05;
    pot -= rake;

    double total = 0;
    for (double s : stacks)
      if (s > 0) total += s;
    std::uniform_real_distribution<double> dist(0, total);
    double roll = dist(rng_), cum = 0;
    int winner = 0;
    for (size_t i = 0; i < stacks.size(); i++) {
      cum += stacks[i];
      if (roll <= cum && stacks[i] > 0) {
        winner = static_cast<int>(i);
        break;
      }
    }
    stacks[winner] += pot;
  }

  for (size_t i = 0; i < agents_.size(); i++)
    result.payouts.push_back({static_cast<int>(i), stacks[i] - agents_[i].starting_stack});
  return result;
}

SimOutcome MultiAgentSimulator::RunRoundRobin() {
  SimOutcome outcome;
  outcome.total_hands = num_hands_per_matchup_;
  int n = static_cast<int>(agents_.size());
  std::map<std::string, double> total_won;
  for (int i = 0; i < n; i++) total_won[agents_[i].name] = 0;

  for (int i = 0; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      auto res = SimulateMatchup(agents_[i], agents_[j], num_hands_per_matchup_, false);
      double eq = HeadToHeadEquity(agents_[i].strategy, agents_[j].strategy, 2000);

      outcome.matchup_matrix[agents_[i].name][agents_[j].name] = eq;
      outcome.matchup_matrix[agents_[j].name][agents_[i].name] = 1.0 - eq;

      total_won[agents_[i].name] += res.payouts[0].second;
      total_won[agents_[j].name] += res.payouts[1].second;

      if (verbose_)
        std::cout << "  " << agents_[i].name << " vs " << agents_[j].name << " | " << int(eq * 100)
                  << "%\n";
    }
  }

  for (auto& [name, won] : total_won) outcome.final_equity.push_back({name, won});
  std::sort(outcome.final_equity.begin(), outcome.final_equity.end(),
            [](auto& a, auto& b) { return a.second > b.second; });
  for (auto& [name, eq] : outcome.final_equity)
    outcome.rankings.push_back({name, static_cast<int>(eq)});

  return outcome;
}

}  // namespace phase8
}  // namespace poker_engine
