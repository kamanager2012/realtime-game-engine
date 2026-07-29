#pragma once
#include <map>
#include <string>
#include <vector>

#include "poker_engine/phase14/database.h"

namespace poker_engine::phase14 {

// ========== 比赛统计 ==========
struct SessionStats {
  int64_t session_id = 0;
  std::string start_time;
  std::string end_time;
  int hand_count = 0;
  std::string table_name;
  double total_buy_in = 0;

  std::string ToString() const;
};

// ========== 位置统计 ==========
struct PositionalStats {
  int seat = 0;
  int hands_played = 0;
  int hands_won = 0;
  double net_profit = 0;
  double vpip = 0;
  double pfr = 0;
  double win_rate = 0;
  double bb_per_100 = 0;

  std::string ToString() const;
};

// ========== 统计仓库 ==========
class StatRepository {
 public:
  explicit StatRepository(class Database& db, class HandRepository& hand_repo,
                          class PlayerRepository& player_repo);

  // === 汇总统计 ===
  struct OverviewStats {
    int64_t total_hands = 0;
    int64_t total_wins = 0;
    double win_rate = 0;
    double avg_net_per_hand = 0;
    double avg_bb_per_100 = 0;
    int64_t total_sessions = 0;
    double total_buy_in = 0;
    double total_cash_out = 0;
    double roi = 0;
  };
  OverviewStats GetOverviewStats();

  // 按位置统计
  std::vector<PositionalStats> GetPositionalStats(int32_t player_id);

  // 按对手统计
  struct OpponentStat {
    int32_t opponent_id = 0;
    std::string opponent_name;
    int hands_played = 0;
    int hands_won_vs = 0;
    double net_vs = 0;
    double bb_per_100_vs = 0;
    double win_rate_vs = 0;
  };
  std::vector<OpponentStat> GetOpponentStats(int32_t player_id);

  // 按时间段统计
  struct TimeRangeStats {
    std::string period;
    int hands = 0;
    double net = 0;
    double bb_per_100 = 0;
    double win_rate = 0;
  };
  std::vector<TimeRangeStats> GetDailyStats(int32_t player_id);
  std::vector<TimeRangeStats> GetWeeklyStats(int32_t player_id);
  std::vector<TimeRangeStats> GetMonthlyStats(int32_t player_id);

  // 分牌型统计
  struct HandTypeStats {
    std::string hand_category;
    int hands = 0;
    int wins = 0;
    double win_rate = 0;
    double avg_net = 0;
  };
  std::vector<HandTypeStats> GetHandTypeStats(int32_t player_id);

  // 运行方差 / 置信区间
  struct VarianceStats {
    double mean_bb100 = 0;
    double std_dev_bb100 = 0;
    double ci95_low = 0;
    double ci95_high = 0;
    double max_drawdown = 0;
    double max_drawdown_pct = 0;
    double sharpe_ratio = 0;
    int sample_size = 0;
  };
  VarianceStats GetVarianceStats(int32_t player_id);

  // 会话统计
  std::vector<SessionStats> GetSessionStats();

 private:
  Database& db_;
  class HandRepository& hand_repo_;
  class PlayerRepository& player_repo_;

  double SafeDivide(double a, double b) const;
  std::string GetHandCategory(const std::string& hole_cards) const;
};

}  // namespace poker_engine::phase14
