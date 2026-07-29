#include "poker_engine/phase14/account_repository.h"

#include <sstream>

#include "poker_engine/phase14/database.h"

namespace poker_engine::phase14 {

namespace {

// Parameterized lookups — never build SQL by concatenating user input.
std::optional<AccountData> QueryAccountById(Database& db, int64_t pid) {
  auto rs = db.Prepare(
      "SELECT id, username, display_name, password_hash, chips, total_profit, "
      "hands_played, elo_rating, avatar_url, created_at, last_login "
      "FROM accounts WHERE id = ?");
  StatementBinder(rs).Bind(1, pid);
  if (rs.Next()) {
    auto r = rs.GetRow();
    AccountData account;
    account.id = r.GetInt64(0);
    account.username = r.GetString(1);
    account.display_name = r.GetString(2);
    account.password_hash = r.GetString(3);
    account.chips = r.GetInt64(4);
    account.total_profit = r.GetInt64(5);
    account.hands_played = r.GetInt64(6);
    account.elo_rating = r.GetInt64(7);
    account.avatar_url = r.GetString(8);
    account.created_at = r.GetString(9);
    account.last_login = r.GetString(10);
    return account;
  }
  return std::nullopt;
}

std::optional<AccountData> QueryAccountByUsername(Database& db, const std::string& username) {
  auto rs = db.Prepare(
      "SELECT id, username, display_name, password_hash, chips, total_profit, "
      "hands_played, elo_rating, avatar_url, created_at, last_login "
      "FROM accounts WHERE username = ?");
  StatementBinder(rs).Bind(1, username);
  if (rs.Next()) {
    auto r = rs.GetRow();
    AccountData account;
    account.id = r.GetInt64(0);
    account.username = r.GetString(1);
    account.display_name = r.GetString(2);
    account.password_hash = r.GetString(3);
    account.chips = r.GetInt64(4);
    account.total_profit = r.GetInt64(5);
    account.hands_played = r.GetInt64(6);
    account.elo_rating = r.GetInt64(7);
    account.avatar_url = r.GetString(8);
    account.created_at = r.GetString(9);
    account.last_login = r.GetString(10);
    return account;
  }
  return std::nullopt;
}

}  // namespace


AccountRepository::AccountRepository(Database& db) : db_(db) {}

bool AccountRepository::SaveAccount(const AccountData& account) {
  auto rs = db_.Prepare(
      "INSERT OR REPLACE INTO accounts "
      "(id, username, display_name, password_hash, chips, total_profit, "
      "hands_played, elo_rating, avatar_url) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
  StatementBinder(rs)
      .Bind(1, account.id)
      .Bind(2, account.username)
      .Bind(3, account.display_name)
      .Bind(4, account.password_hash)
      .Bind(5, account.chips)
      .Bind(6, account.total_profit)
      .Bind(7, account.hands_played)
      .Bind(8, account.elo_rating)
      .Bind(9, account.avatar_url);
  rs.Next();

  if (mirror_callback_) mirror_callback_(account);

  // Seed an initial, append-only ledger entry recording the opening
  // balance, so wallet_transactions is self-contained and fully
  // reconcilable against accounts.chips.
  auto seed = db_.Prepare(
      "INSERT INTO wallet_transactions (player_id, tx_type, amount, "
      "balance_after, reference) VALUES (?, 'grant', ?, ?, 'account_creation')");
  StatementBinder(seed).Bind(1, account.id).Bind(2, account.chips).Bind(3, account.chips);
  seed.Next();
  return true;
}

void AccountRepository::SetAccountMirrorCallback(std::function<void(const AccountData&)> cb) {
  mirror_callback_ = std::move(cb);
}

std::optional<AccountData> AccountRepository::FindByUsername(const std::string& username) {
  return QueryAccountByUsername(db_, username);
}

std::optional<AccountData> AccountRepository::FindById(int64_t player_id) {
  return QueryAccountById(db_, player_id);
}

bool AccountRepository::UpdateLastLogin(int64_t player_id) {
  auto rs = db_.Prepare("UPDATE accounts SET last_login = datetime('now') WHERE id = ?");
  StatementBinder(rs).Bind(1, player_id);
  rs.Next();
  return true;
}

bool AccountRepository::UpdateStats(int64_t player_id, int64_t chips, int64_t total_profit,
                                     int64_t hands_played, int64_t elo_rating) {
  auto rs = db_.Prepare(
      "UPDATE accounts SET chips = ?, total_profit = ?, hands_played = ?, elo_rating = ? "
      "WHERE id = ?");
  StatementBinder(rs)
      .Bind(1, chips)
      .Bind(2, total_profit)
      .Bind(3, hands_played)
      .Bind(4, elo_rating)
      .Bind(5, player_id);
  rs.Next();
  return true;
}

bool AccountRepository::DeleteAccount(int64_t player_id) {
  // 1) 匿名化账户 PII
  std::string anon_name = "deleted_" + std::to_string(player_id);
  auto anon = db_.Prepare(
      "UPDATE accounts SET username = ?, display_name = '', password_hash = '', "
      "avatar_url = '', chips = 0 WHERE id = ?");
  StatementBinder(anon).Bind(1, anon_name).Bind(2, player_id);
  anon.Next();
  // 2) 删除该玩家钱包流水（免费娱乐版无资金审计留存义务）
  auto del = db_.Prepare("DELETE FROM wallet_transactions WHERE player_id = ?");
  StatementBinder(del).Bind(1, player_id);
  del.Next();
  return true;
}

AccountRepository::DailyBonus AccountRepository::GetDailyBonus(int64_t player_id) {
  DailyBonus result;
  auto rs = db_.Prepare("SELECT last_claim, streak FROM daily_bonus WHERE player_id = ?");
  StatementBinder(rs).Bind(1, player_id);
  if (rs.Next()) {
    result.last_claim = rs.GetRow().GetString(0);
    result.streak = rs.GetRow().GetInt64(1);
  }
  return result;
}

bool AccountRepository::UpdateDailyBonus(int64_t player_id, const std::string& date,
                                       int64_t streak) {
  // INSERT OR REPLACE：首次领取插入，之后覆盖
  auto rs = db_.Prepare(
      "INSERT OR REPLACE INTO daily_bonus (player_id, last_claim, streak) VALUES (?, ?, ?)");
  StatementBinder(rs).Bind(1, player_id).Bind(2, date).Bind(3, streak);
  rs.Next();
  return true;
}

namespace {
// 成就定义（首次启动时种子化）
struct AchDef { const char* id; const char* name; const char* desc; const char* icon; };
const AchDef kAchievements[] = {
    {"first_win", "首胜", "赢下你的第一手牌", "🏆"},
    {"hands_10", "新手上路", "完成 10 手牌", "🌱"},
    {"hands_100", "牌桌老手", "完成 100 手牌", "🔥"},
    {"streak_3", "三连胜", "连续赢下 3 手牌", "⚡"},
    {"streak_5", "连胜狂人", "连续赢下 5 手牌", "👑"},
};
}  // namespace

void AccountRepository::SeedAchievements() {
  auto rs = db_.Query("SELECT COUNT(*) FROM achievements_def");
  if (rs.Next() && rs.GetRow().GetInt64(0) > 0) return;  // 已种子化
  for (const auto& a : kAchievements) {
    std::string sql = "INSERT OR IGNORE INTO achievements_def (id, name, desc, icon) VALUES ('" +
                       std::string(a.id) + "', '" + std::string(a.name) + "', '" +
                       std::string(a.desc) + "', '" + std::string(a.icon) + "')";
    db_.Execute(sql);
  }
}

std::vector<AccountRepository::Achievement> AccountRepository::GetAchievements(int64_t player_id) {
  std::vector<Achievement> out;
  auto rs = db_.Query("SELECT id, name, desc, icon FROM achievements_def ORDER BY id");
  while (rs.Next()) {
    Achievement a;
    a.id = rs.GetRow().GetString(0);
    a.name = rs.GetRow().GetString(1);
    a.desc = rs.GetRow().GetString(2);
    a.icon = rs.GetRow().GetString(3);
    out.push_back(std::move(a));
  }
  // 标记已解锁
  auto ur = db_.Query("SELECT achievement_id FROM player_achievements WHERE player_id = " +
                      std::to_string(player_id));
  while (ur.Next()) {
    std::string aid = ur.GetRow().GetString(0);
    for (auto& a : out)
      if (a.id == aid) { a.unlocked = true; break; }
  }
  return out;
}

std::vector<std::string> AccountRepository::CheckAchievements(int64_t player_id,
                                                         int64_t hands_played, bool won_last,
                                                         int64_t win_streak) {
  std::vector<std::string> newly;
  auto unlocked = [&](const char* id) {
    std::string sel = "SELECT 1 FROM player_achievements WHERE player_id = " +
                      std::to_string(player_id) + " AND achievement_id = '" + id + "'";
    auto r = db_.Query(sel);
    return r.Next();
  };
  auto unlock = [&](const char* id) {
    if (unlocked(id)) return;
    std::string ins = "INSERT OR IGNORE INTO player_achievements (player_id, achievement_id) VALUES (" +
                      std::to_string(player_id) + ", '" + id + "')";
    if (db_.Execute(ins)) newly.push_back(id);
  };

  if (won_last) unlock("first_win");
  if (hands_played >= 10) unlock("hands_10");
  if (hands_played >= 100) unlock("hands_100");
  if (win_streak >= 3) unlock("streak_3");
  if (win_streak >= 5) unlock("streak_5");
  return newly;
}

std::vector<AccountData> AccountRepository::LoadAll() {
  std::vector<AccountData> result;
  std::string sql = "SELECT id, username, display_name, password_hash, chips, "
                    "total_profit, hands_played, elo_rating, avatar_url, "
                    "created_at, last_login FROM accounts";
  auto rs = db_.Query(sql);
  while (rs.Next()) {
    AccountData account;
    auto r = rs.GetRow();
    account.id = r.GetInt64(0);
    account.username = r.GetString(1);
    account.display_name = r.GetString(2);
    account.password_hash = r.GetString(3);
    account.chips = r.GetInt64(4);
    account.total_profit = r.GetInt64(5);
    account.hands_played = r.GetInt64(6);
    account.elo_rating = r.GetInt64(7);
    account.avatar_url = r.GetString(8);
    account.created_at = r.GetString(9);
    account.last_login = r.GetString(10);
    result.push_back(std::move(account));
  }
  return result;
}

}  // namespace poker_engine::phase14
