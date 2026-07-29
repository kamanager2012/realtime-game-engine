#pragma once
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace poker_engine {
namespace phase7 {

struct TourneyPlayer {
  std::string name;
  double chips = 0;
  double equity = 0;
  double icm_dollar = 0;
  double bubble_pressure = 0;
};

struct TourneyConfig {
  int max_players = 6;
  double starting_stack = 1500;
  double current_blind_level = 1;
  double big_blind = 100;
  double ante = 10;
  int players_remaining = 0;
  int total_starting = 0;
  double total_prize_pool = 0;
};

struct PushFoldAdvice {
  double eff_stack;
  double pot_odds;
  double push_min_equity;
  double call_min_equity;
  std::string action;
  double fold_equity;
  double icm_pressure;
  std::string ToString() const;
};

struct TourneyICMResult {
  std::vector<TourneyPlayer> players;
  std::vector<PushFoldAdvice> push_fold_table;
  double total_equity = 0;
  double bubble_factor_avg = 0;
  bool on_bubble = false;
  std::string ToString() const;
};

class TournamentICM {
 public:
  TournamentICM() = default;
  explicit TournamentICM(const TourneyConfig& cfg);
  void SetConfig(const TourneyConfig& cfg);
  void AddPlayer(const std::string& name, double chips);
  void SetPayoutSchedule(const std::vector<double>& payouts);
  TourneyICMResult Calculate();
  TourneyICMResult Calculate(double max_prize_pool);
  std::vector<PushFoldAdvice> PushFoldTable(double short_stack, double pot,
                                            bool is_heads_up = false);
  double PlayerEquity(const TourneyICMResult& result, const std::string& name) const;
  double NashPushRange(double m_value, double bb);
  double NashCallRange(double m_value, double bb, double pot_odds);
  struct BubbleAnalysis {
    double elimination_probability;
    double expected_doublings;
    std::string suggested_action;
  };
  BubbleAnalysis AnalyzeBubble(double stack, int position);

 private:
  TourneyConfig config_;
  std::vector<std::pair<std::string, double>> players_;
  std::vector<double> payouts_;
  void ICMRecursive(std::vector<double>& equity, std::vector<double>& chips, double sum, int depth,
                    double prob, const std::vector<double>& payouts);
};

}  // namespace phase7
}  // namespace poker_engine
