#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace poker_engine::phase14 {

class Database;

// ========== 账户数据（独立于 phase13 PlayerAccount） ==========
struct AccountData {
  int64_t id = 0;
  std::string username;
  std::string display_name;
  std::string password_hash;  // salt$pbkdf2 format
  int64_t chips = 1000;
  int64_t total_profit = 0;
  int64_t hands_played = 0;
  int64_t elo_rating = 1500;
  int role = 0;  // PlayerRole enum cast
  std::string avatar_url;
  std::string created_at;
  std::string last_login;
};

// ========== 账户持久化仓库（抽象接口） ==========
class IAccountRepository {
 public:
  virtual ~IAccountRepository() = default;

  virtual bool SaveAccount(const AccountData& account) = 0;
  virtual std::optional<AccountData> FindByUsername(const std::string& username) = 0;
  virtual std::optional<AccountData> FindById(int64_t player_id) = 0;
  virtual bool UpdateLastLogin(int64_t player_id) = 0;
  virtual bool UpdateStats(int64_t player_id, int64_t chips, int64_t total_profit,
                           int64_t hands_played, int64_t elo_rating) = 0;
  virtual bool DeleteAccount(int64_t player_id) = 0;
  virtual std::vector<AccountData> LoadAll() = 0;
};

// ========== 账户持久化仓库（SQLite 实现） ==========
class AccountRepository : public IAccountRepository {
 public:
  explicit AccountRepository(Database& db);

  bool SaveAccount(const AccountData& account) override;
  void SetAccountMirrorCallback(std::function<void(const AccountData&)> cb);

  std::optional<AccountData> FindByUsername(const std::string& username) override;
  std::optional<AccountData> FindById(int64_t player_id) override;
  bool UpdateLastLogin(int64_t player_id) override;
  bool UpdateStats(int64_t player_id, int64_t chips, int64_t total_profit,
                   int64_t hands_played, int64_t elo_rating) override;
  bool DeleteAccount(int64_t player_id) override;

  // Extended API (not in IAccountRepository)
  struct DailyBonus {
    std::string last_claim;  // 上次领取日期 YYYY-MM-DD，空表示从未
    int64_t streak = 0;      // 连续领取天数
  };
  DailyBonus GetDailyBonus(int64_t player_id);
  bool UpdateDailyBonus(int64_t player_id, const std::string& date, int64_t streak);

  // 成就系统：首次启动时种子化成就定义；按玩家查询/检查解锁
  struct Achievement {
    std::string id;
    std::string name;
    std::string desc;
    std::string icon;
    bool unlocked = false;
  };
  void SeedAchievements();
  std::vector<Achievement> GetAchievements(int64_t player_id);
  // 依据当前计数检查并解锁；返回本次新解锁的成就 id 列表
  std::vector<std::string> CheckAchievements(int64_t player_id, int64_t hands_played,
                                                bool won_last, int64_t win_streak);

  // Load all accounts (for AuthService startup cache fill)
  std::vector<AccountData> LoadAll();

 private:
  Database& db_;
  std::function<void(const AccountData&)> mirror_callback_;
};

}  // namespace poker_engine::phase14
