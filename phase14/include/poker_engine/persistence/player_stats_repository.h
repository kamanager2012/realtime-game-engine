#pragma once

#include <optional>
#include <string>
#include <vector>

namespace poker_engine::persistence {

struct PlayerStatsEntry {
  int64_t player_id;
  std::string username;
  std::string display_name;
  int64_t hands_played;
  int64_t hands_won;
  double win_rate;
  double vpip_pct;
  double pfr_pct;
  double agg_factor;
  int64_t total_profit;
  int64_t elo_rating;
};

class PlayerStatsRepository {
 public:
  explicit PlayerStatsRepository(class DatabaseManager& db);

  // 从手牌历史重新计算所有统计
  void RecalculateAll();

  // 增量更新单个玩家
  void UpdateFromHand(int64_t player_id, bool won, int64_t profit, bool acted_preflop,
                      bool raised_preflop);

  // 查询
  std::vector<PlayerStatsEntry> GetLeaderboard(int limit = 100) const;
  std::optional<PlayerStatsEntry> GetStats(int64_t player_id) const;

 private:
  DatabaseManager& db_;
};

}  // namespace poker_engine::persistence
