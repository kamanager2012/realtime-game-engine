#pragma once

#include <optional>
#include <string>
#include <vector>

#include "poker_engine/game/player_state.h"
#include "poker_engine/network/auth_service.h"

namespace poker_engine::persistence {

class DatabaseManager;  // forward declaration

struct PlayerRecord {
  int64_t id;
  std::string username;
  std::string display_name;
  int64_t chips;
  int64_t total_profit;
  int64_t hands_played;
  int64_t elo_rating;
};

struct PlayerStats {
  int64_t hands_played = 0;
  int64_t hands_won = 0;
  double total_profit = 0.0;
  double vpip_pct = 0.0;    // 自愿投入底池率
  double pfr_pct = 0.0;     // 翻前加注率
  double agg_factor = 0.0;  // 侵略因子
  double win_rate = 0.0;
};

class IPlayerRepository {
 public:
  virtual ~IPlayerRepository() = default;

  // 创建玩家
  virtual int64_t Create(const std::string& username, const std::string& display_name,
                         const std::string& password_hash) = 0;

  // 按 ID 查询
  virtual std::optional<PlayerRecord> GetById(int64_t player_id) = 0;

  // 按用户名查询
  virtual std::optional<PlayerRecord> GetByUsername(const std::string& username) = 0;

  // 更新筹码
  virtual bool UpdateChips(int64_t player_id, int64_t new_chips) = 0;

  // 更新统计
  virtual bool UpdateStats(int64_t player_id, const PlayerStats& stats) = 0;

  // 排行榜
  virtual std::vector<PlayerRecord> GetLeaderboard(int limit = 100) = 0;

  // 检查用户名是否存在
  virtual bool Exists(const std::string& username) = 0;
};

// SQLite 实现
class SQLitePlayerRepository : public IPlayerRepository {
 public:
  explicit SQLitePlayerRepository(DatabaseManager& db) : db_(db) {}

  int64_t Create(const std::string& username, const std::string& display_name,
                 const std::string& password_hash) override;

  std::optional<PlayerRecord> GetById(int64_t player_id) override;
  std::optional<PlayerRecord> GetByUsername(const std::string& username) override;

  bool UpdateChips(int64_t player_id, int64_t new_chips) override;
  bool UpdateStats(int64_t player_id, const PlayerStats& stats) override;
  std::vector<PlayerRecord> GetLeaderboard(int limit) override;
  bool Exists(const std::string& username) override;

 private:
  DatabaseManager& db_;
};

}  // namespace poker_engine::persistence
