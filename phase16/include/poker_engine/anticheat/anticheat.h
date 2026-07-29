#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "poker_engine/replay/replay_types.h"

namespace poker_engine::anticheat {

enum class SuspicionLevel : uint8_t { Clean = 0, Low = 1, Medium = 2, High = 3, Confirmed = 4 };

struct CheatAlert {
  int64_t player_id;
  std::string player_name;
  SuspicionLevel level;
  std::string reason;
  std::string evidence;
  double score;
  int64_t timestamp;

  std::string ToString() const;
};

struct PlayerStatistics {
  int64_t player_id;
  std::string player_name;

  int hands_played = 0;
  int hands_won = 0;
  int64_t total_profit = 0;

  int vpip_hands = 0;
  double vpip_pct = 0.0;

  int pfr_hands = 0;
  double pfr_pct = 0.0;

  int bets_raises = 0;
  int calls = 0;
  double agg_factor = 0.0;

  int three_bet_hands = 0;
  double three_bet_pct = 0.0;

  struct PositionalStats {
    int hands = 0;
    int vpip = 0;
    int pfr = 0;
    int aggr = 0;
  };
  PositionalStats early;
  PositionalStats middle;
  PositionalStats late;
  PositionalStats blind;

  std::unordered_map<int64_t, int> same_table_counts;
  std::unordered_map<int64_t, int> adjacent_seat_counts;

  std::vector<double> bet_sizing_history;
  std::vector<int64_t> response_times_ms;

  double estimated_range_pct = 0.0;

  void ComputeRatios();

  static double ZScore(double value, double mean, double stddev);
  static double Mean(const std::vector<double>& values);
  static double StdDev(const std::vector<double>& values, double mean);
};

class CollusionDetector {
 public:
  struct CollusionConfig {
    double same_table_threshold = 0.3;
    double adjacent_seat_threshold = 0.2;
    double suspicious_action_threshold = 0.15;
    double min_hands_for_analysis = 30;
    double soft_play_threshold = 0.1;
  };

  explicit CollusionDetector(const CollusionConfig& config);
  CollusionDetector();  // default config

  void AddPlayerStats(const PlayerStatistics& stats);

  struct PlayerPairAnalysis {
    int64_t player_a;
    int64_t player_b;
    double same_table_rate;
    double adjacent_seat_rate;
    double mutual_fold_rate;
    double suspicious_bet_timing;
    double overall_score;
    std::string evidence;

    SuspicionLevel GetLevel() const;
  };

  std::vector<PlayerPairAnalysis> AnalyzeAllPairs();
  const std::vector<PlayerStatistics>& GetPlayers() const { return players_; }

 private:
  CollusionConfig config_;
  std::vector<PlayerStatistics> players_;

  double ComputeSameTableRate(const PlayerStatistics& a, const PlayerStatistics& b) const;
  double ComputeAdjacentSeatRate(const PlayerStatistics& a, const PlayerStatistics& b) const;
  double ComputeSuspiciousActions(const PlayerStatistics& a, const PlayerStatistics& b) const;
  double ComputeMutualFoldRate(const PlayerStatistics& a, const PlayerStatistics& b) const;

  PlayerPairAnalysis AnalyzePair(const PlayerStatistics& a, const PlayerStatistics& b) const;
};

class BotDetector {
 public:
  struct BotConfig {
    double response_time_consistency_threshold = 0.15;
    double optimal_play_threshold = 0.92;
    double bet_sizing_precision = 0.05;
    double min_hands_for_analysis = 50;
    double position_awareness_threshold = 0.8;
    double reaction_time_fluctuation_max = 0.3;
  };

  explicit BotDetector(const BotConfig& config);
  BotDetector();  // default config

  void AddPlayerStats(const PlayerStatistics& stats);

  struct BotAnalysis {
    int64_t player_id;
    double response_consistency_score;
    double optimal_play_score;
    double bet_sizing_score;
    double position_awareness;
    double overall_bot_probability;
    std::vector<std::string> flags;
    std::string evidence;

    SuspicionLevel GetLevel() const;
  };

  BotAnalysis AnalyzePlayer(const PlayerStatistics& stats) const;
  std::vector<BotAnalysis> AnalyzeAll();

 private:
  BotConfig config_;
  std::vector<PlayerStatistics> players_;

  double ComputeResponseConsistency(const PlayerStatistics& stats) const;
  double ComputeOptimalPlayRatio(const PlayerStatistics& stats) const;
  double ComputeBetSizingPrecision(const PlayerStatistics& stats) const;
  double ComputePositionAwareness(const PlayerStatistics& stats) const;
};

class AntiCheatManager {
 public:
  using AlertCallback = std::function<void(const CheatAlert&)>;

  AntiCheatManager();

  void SetAlertCallback(AlertCallback cb) { alert_callback_ = std::move(cb); }

  void SubmitHandData(const replay::HandSnapshot& hand);
  void SubmitHandBatch(const std::vector<replay::HandSnapshot>& hands);
  void RunAnalysis();

  const std::vector<CheatAlert>& GetAlerts() const { return alerts_; }
  double GetPlayerSuspicionScore(int64_t player_id) const;
  const PlayerStatistics* GetPlayerStats(int64_t player_id) const;
  void Reset();

 private:
  CollusionDetector collusion_detector_;
  BotDetector bot_detector_;
  std::unordered_map<int64_t, PlayerStatistics> player_stats_;
  std::vector<CheatAlert> alerts_;
  AlertCallback alert_callback_;

  void UpdatePlayerStats(const replay::HandSnapshot& hand);
  void GenerateAlert(int64_t player_id, SuspicionLevel level, const std::string& reason,
                     const std::string& evidence);
};

}  // namespace poker_engine::anticheat
