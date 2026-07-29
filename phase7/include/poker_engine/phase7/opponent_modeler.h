#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "poker_engine/phase4/hh_parser.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase7 {

struct OpponentStats {
  std::string player_name;
  int64_t hands_seen = 0;
  int64_t hands_dealt = 0;
  int64_t vpip_count = 0;
  int64_t pfr_count = 0;
  int64_t three_bet_count = 0;
  int64_t four_bet_count = 0;
  int64_t c_bet_count = 0;
  int64_t c_bet_possible = 0;
  int64_t fold_to_cbet = 0;
  int64_t fold_to_turn_bet = 0;
  int64_t check_raise_count = 0;
  int64_t donk_bet_count = 0;
  std::vector<double> bet_sizes;
  std::vector<double> raise_sizes;

  double vpip_pct() const { return hands_dealt > 0 ? 100.0 * vpip_count / hands_dealt : 0; }
  double pfr_pct() const { return hands_dealt > 0 ? 100.0 * pfr_count / hands_dealt : 0; }
  double three_bet_pct() const { return pfr_count > 0 ? 100.0 * three_bet_count / pfr_count : 0; }
  double cbet_pct() const { return c_bet_possible > 0 ? 100.0 * c_bet_count / c_bet_possible : 0; }
  double fold_cbet_pct() const { return c_bet_count > 0 ? 100.0 * fold_to_cbet / c_bet_count : 0; }
  double check_raise_pct() const {
    return check_raise_count > 0 ? 100.0 * check_raise_count / hands_seen : 0;
  }
  double avg_bet_size() const;
  double avg_raise_size() const;
  double aggression_factor() const;
  double aggression_frequency() const;
  std::string classify() const;
  std::string ToString() const;
};

struct OpponentCluster {
  std::string label;
  double vpip_min, vpip_max;
  double pfr_min, pfr_max;
  double agg_min, agg_max;
  std::vector<std::string> members;
  std::string ToString() const;
};

struct OpponentPrediction {
  std::string player_name;
  double vpip_confidence;
  double pfr_confidence;
  double cbet_confidence;
  std::string likely_type;
  std::vector<std::pair<std::string, double>> type_probabilities;
  std::string ToString() const;
};

class OpponentModeler {
 public:
  OpponentModeler();
  void ProcessHand(const poker_engine::phase4::HandHistory& hh, bool is_hero = false);
  void ProcessHands(const std::vector<poker_engine::phase4::HandHistory>& hands);
  void ProcessDirectory(const std::string& dir_path);
  OpponentStats GetStats(const std::string& player_name) const;
  OpponentPrediction PredictStyle(const std::string& player_name) const;
  std::vector<OpponentCluster> ClusterPlayers(int num_clusters = 5) const;
  poker_engine::range::Range EstimateRange(const std::string& player, int street,
                                           const std::string& last_action) const;
  std::string PlayerReport(const std::string& player_name) const;
  std::string SessionReport() const;
  std::string FullReport() const;
  const std::map<std::string, OpponentStats>& AllStats() const { return stats_; }
  int PlayerCount() const { return static_cast<int>(stats_.size()); }
  int64_t TotalHandsProcessed() const { return total_hands_; }

 private:
  std::map<std::string, OpponentStats> stats_;
  int64_t total_hands_ = 0;
  OpponentPrediction BuildPrediction(const OpponentStats& st) const;
  static std::string Classify(const OpponentStats& st);
  std::vector<std::vector<double>> FeatureVectors() const;
  std::vector<int> KMeans(int k, const std::vector<std::vector<double>>& points) const;
};

}  // namespace phase7
}  // namespace poker_engine
