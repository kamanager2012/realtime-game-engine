#pragma once
#include <map>
#include <string>
#include <vector>

namespace poker_engine::phase14 {

// ========== 玩家信息 ==========
struct PlayerInfo {
  int32_t player_id = 0;
  std::string name;
  std::string display_name;
  double total_buy_in = 0;
  double total_cash_out = 0;
  int64_t hands_played = 0;
  int64_t hands_won = 0;
  double total_net = 0;
  std::string created_at;
  std::string last_seen;
  std::string notes;

  std::string ToString() const;
};

// ========== 累计统计 ==========
struct PlayerStats {
  int32_t player_id = 0;
  std::string player_name;

  int hands_vpip = 0;
  int hands_seen = 0;
  double vpip_pct = 0;

  int hands_pfr = 0;
  double pfr_pct = 0;

  int bets = 0;
  int calls = 0;
  double af = 0;

  int hands_won = 0;
  double win_rate = 0;

  double total_net = 0;
  double avg_bb_per_100 = 0;

  int64_t total_wagered = 0;

  std::string ToString() const;
};

// ========== 玩家仓库 ==========
class PlayerRepository {
 public:
  explicit PlayerRepository(class Database& db);

  // 创建/注册玩家
  int32_t CreatePlayer(const std::string& name, const std::string& display_name = "");

  // 获取玩家
  PlayerInfo GetPlayer(int32_t player_id);

  // 按名字查找
  std::vector<PlayerInfo> FindPlayers(const std::string& name_pattern);

  // 更新累计信息
  void UpdatePlayerStats(int32_t player_id, double buy_in, double cash_out);

  // 获取所有玩家
  std::vector<PlayerInfo> GetAllPlayers();

  // 获取活跃玩家
  std::vector<PlayerInfo> GetActivePlayers(int min_hands = 5);

  // 计算统计
  PlayerStats CalculateStats(int32_t player_id);

  // 批量计算统计
  std::vector<PlayerStats> CalculateAllStats(int min_hands = 10);

  // 排行榜
  std::vector<std::pair<PlayerInfo, PlayerStats>> GetLeaderboard(int limit = 20);

 private:
  class Database& db_;
};

}  // namespace poker_engine::phase14
