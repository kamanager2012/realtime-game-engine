#pragma once

#include "poker_engine/base/result.h"
#include "poker_engine/persistence/safe_database.h"
#include "poker_engine/persistence/safe_query.h"

namespace poker_engine::persistence {

struct Player {
  int64_t player_id = 0;
  std::string username;
  std::string display_name;
  int64_t chips = 0;
  int64_t total_profit = 0;
  int64_t hands_played = 0;
  std::string created_at;
};

class SafePlayerRepository {
 public:
  explicit SafePlayerRepository(SafeDatabase& db);

  // 创建玩家（预处理语句，无 SQL 注入）
  base::Result<int64_t> Create(const std::string& username, const std::string& display_name,
                               const std::string& password_hash);

  // 按 ID 获取（参数化查询）
  base::Result<std::optional<Player>> GetById(int64_t player_id) const;

  // 按用户名获取（参数化查询）
  base::Result<std::optional<Player>> GetByUsername(const std::string& username) const;

  // 更新筹码（事务保护 + 参数化）
  base::Result<bool> UpdateChips(int64_t player_id, int64_t new_balance);

  // 原子增减筹码（防止竞争）
  base::Result<int64_t> AdjustChips(int64_t player_id, int64_t delta);

  // 排行榜（参数化 + 限制返回行数）
  base::Result<std::vector<Player>> GetLeaderboard(int limit = 50) const;

  // 搜索玩家
  base::Result<std::vector<Player>> SearchByName(const std::string& pattern) const;

  // 批量更新（事务保护）
  base::Result<bool> UpdateBatch(const std::vector<Player>& players);

  // 只读查询
  base::Result<int64_t> GetTotalPlayerCount() const;
  base::Result<std::vector<Player>> GetRecentlyActive(int limit = 20) const;

 private:
  SafeDatabase& db_;

  // 辅助：验证用户名格式（防止注入 + 业务规则）
  static bool IsValidUsername(const std::string& username);
  static bool IsValidDisplayName(const std::string& display_name);
};

}  // namespace poker_engine::persistence
