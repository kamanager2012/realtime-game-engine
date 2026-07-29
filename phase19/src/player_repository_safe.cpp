#include "poker_engine/persistence/player_repository_safe.h"

#include <regex>

#include "poker_engine/base/logging.h"

namespace poker_engine::persistence {

SafePlayerRepository::SafePlayerRepository(SafeDatabase& db) : db_(db) {}

bool SafePlayerRepository::IsValidUsername(const std::string& username) {
  static const std::regex pattern("^[a-zA-Z0-9_]{3,32}$");
  return std::regex_match(username, pattern);
}

bool SafePlayerRepository::IsValidDisplayName(const std::string& display_name) {
  if (display_name.empty() || display_name.size() > 50) return false;
  for (char c : display_name) {
    if (static_cast<unsigned char>(c) < 0x20 && c != '\t') return false;
  }
  return true;
}

base::Result<int64_t> SafePlayerRepository::Create(const std::string& username,
                                                   const std::string& display_name,
                                                   const std::string& password_hash) {
  if (!IsValidUsername(username)) {
    PE_LOG_WARN("Invalid username format: {}", username);
    return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::InvalidArgument));
  }

  if (!IsValidDisplayName(display_name)) {
    PE_LOG_WARN("Invalid display name format");
    return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::InvalidArgument));
  }

  auto txn = db_.BeginTransaction();

  // 检查用户名是否已存在（参数化查询）
  auto check_prep = db_.Prepare("SELECT id FROM players WHERE username = ?");
  if (!check_prep.IsOk()) {
    txn.Rollback();
    return base::Result<int64_t>::Err(check_prep.Error());
  }
  check_prep->Bind(0, username);
  if (check_prep->Step()) {
    txn.Rollback();
    return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::AlreadyExists));
  }
  check_prep->Reset();

  // 插入（参数化）
  auto insert_prep = db_.Prepare(
      "INSERT INTO players (username, display_name, password_hash, "
      "chips, total_profit, hands_played, created_at) "
      "VALUES (?, ?, ?, 1000, 0, 0, datetime('now'))");

  if (!insert_prep.IsOk()) {
    txn.Rollback();
    return base::Result<int64_t>::Err(insert_prep.Error());
  }

  insert_prep->Bind(0, username);
  insert_prep->Bind(1, display_name);
  insert_prep->Bind(2, password_hash);

  if (!insert_prep->Exec()) {
    txn.Rollback();
    PE_LOG_ERROR("Insert failed for username: {}", username);
    return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::DatabaseError));
  }

  auto last_id = db_.Scalar(SafeQuery("SELECT last_insert_rowid()"));

  txn.Commit();

  PE_LOG_INFO("Player created: username={}, id={}", username,
              last_id.IsOk() ? last_id.UnwrapOr(std::nullopt).value_or("") : "err");

  if (last_id.IsOk() && last_id.Unwrap().has_value()) {
    try {
      return base::Result<int64_t>::Ok(std::stoll(*last_id.Unwrap()));
    } catch (...) {
    }
  }
  return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::DatabaseError));
}

base::Result<std::optional<Player>> SafePlayerRepository::GetById(int64_t player_id) const {
  auto prep = db_.Prepare(
      "SELECT id, username, display_name, chips, total_profit, "
      "hands_played, created_at FROM players WHERE id = ?");

  if (!prep.IsOk()) return base::Result<std::optional<Player>>::Err(prep.Error());

  prep->Bind(0, player_id);

  if (!prep->Exec()) {
    return base::Result<std::optional<Player>>::Ok(std::nullopt);
  }

  Player p;
  p.player_id = prep->GetInt64(0);
  p.username = prep->GetString(1);
  p.display_name = prep->GetString(2);
  p.chips = prep->GetInt64(3);
  p.total_profit = prep->GetInt64(4);
  p.hands_played = prep->GetInt64(5);
  p.created_at = prep->GetString(6);

  return base::Result<std::optional<Player>>::Ok(std::move(p));
}

base::Result<std::optional<Player>> SafePlayerRepository::GetByUsername(
    const std::string& username) const {
  auto prep = db_.Prepare(
      "SELECT id, username, display_name, chips, total_profit, "
      "hands_played, created_at FROM players WHERE username = ?");

  if (!prep.IsOk()) return base::Result<std::optional<Player>>::Err(prep.Error());

  prep->Bind(0, username);

  if (!prep->Exec()) {
    return base::Result<std::optional<Player>>::Ok(std::nullopt);
  }

  Player p;
  p.player_id = prep->GetInt64(0);
  p.username = prep->GetString(1);
  p.display_name = prep->GetString(2);
  p.chips = prep->GetInt64(3);
  p.total_profit = prep->GetInt64(4);
  p.hands_played = prep->GetInt64(5);
  p.created_at = prep->GetString(6);

  return base::Result<std::optional<Player>>::Ok(std::move(p));
}

base::Result<bool> SafePlayerRepository::UpdateChips(int64_t player_id, int64_t new_balance) {
  auto result = db_.Execute(
      SafeQuery("UPDATE players SET chips = ? WHERE id = ?").Bind(new_balance).Bind(player_id));

  if (!result.IsOk()) return base::Result<bool>::Err(result.Error());
  return base::Result<bool>::Ok(true);
}

base::Result<int64_t> SafePlayerRepository::AdjustChips(int64_t player_id, int64_t delta) {
  auto prep = db_.Prepare("UPDATE players SET chips = chips + ? WHERE id = ?");

  if (!prep.IsOk()) return base::Result<int64_t>::Err(prep.Error());

  prep->Bind(0, delta);
  prep->Bind(1, player_id);

  if (!prep->Exec()) {
    return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::DatabaseError));
  }

  auto result = db_.Scalar(SafeQuery("SELECT chips FROM players WHERE id = ?").Bind(player_id));

  if (!result.IsOk()) return base::Result<int64_t>::Err(result.Error());
  if (!result.Unwrap().has_value())
    return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::NotFound));
  try {
    return base::Result<int64_t>::Ok(std::stoll(*result.Unwrap()));
  } catch (...) {
    return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::ParseError));
  }
}

base::Result<std::vector<Player>> SafePlayerRepository::GetLeaderboard(int limit) const {
  auto result = db_.Query(SafeQuery("SELECT id, username, display_name, chips, total_profit, "
                                    "hands_played FROM players ORDER BY total_profit DESC LIMIT ?")
                              .Bind(std::max(1, std::min(limit, 100))));

  if (!result.IsOk()) return base::Result<std::vector<Player>>::Err(result.Error());

  std::vector<Player> players;
  for (auto& row : result.Unwrap()) {
    Player p;
    p.player_id = std::stoll(row[0]);
    p.username = row[1];
    p.display_name = row[2];
    p.chips = std::stoll(row[3]);
    p.total_profit = std::stoll(row[4]);
    p.hands_played = std::stoll(row[5]);
    players.push_back(std::move(p));
  }

  return base::Result<std::vector<Player>>::Ok(std::move(players));
}

base::Result<std::vector<Player>> SafePlayerRepository::SearchByName(
    const std::string& pattern) const {
  std::string search = "%" + pattern + "%";

  auto result = db_.Query(SafeQuery("SELECT id, username, display_name, chips, total_profit "
                                    "FROM players WHERE display_name LIKE ? OR username LIKE ? "
                                    "ORDER BY total_profit DESC LIMIT 20")
                              .Bind(search)
                              .Bind(search));

  if (!result.IsOk()) return base::Result<std::vector<Player>>::Err(result.Error());

  std::vector<Player> players;
  for (auto& row : result.Unwrap()) {
    Player p;
    p.player_id = std::stoll(row[0]);
    p.username = row[1];
    p.display_name = row[2];
    p.chips = std::stoll(row[3]);
    p.total_profit = std::stoll(row[4]);
    players.push_back(std::move(p));
  }

  return base::Result<std::vector<Player>>::Ok(std::move(players));
}

base::Result<bool> SafePlayerRepository::UpdateBatch(const std::vector<Player>& players) {
  if (players.empty()) return base::Result<bool>::Ok(true);

  auto txn = db_.BeginTransaction();

  auto prep =
      db_.Prepare("UPDATE players SET chips = ?, total_profit = ?, hands_played = ? WHERE id = ?");

  if (!prep.IsOk()) {
    txn.Rollback();
    return base::Result<bool>::Err(prep.Error());
  }

  for (const auto& p : players) {
    prep->Bind(0, p.chips);
    prep->Bind(1, p.total_profit);
    prep->Bind(2, p.hands_played);
    prep->Bind(3, p.player_id);

    if (!prep->Exec()) {
      PE_LOG_ERROR("Batch update failed for player {}", p.player_id);
      prep->Reset();
      txn.Rollback();
      return base::Result<bool>::Err(base::MakeErrorCode(base::Error::DatabaseError));
    }
    prep->Reset();
  }

  txn.Commit();
  return base::Result<bool>::Ok(true);
}

base::Result<int64_t> SafePlayerRepository::GetTotalPlayerCount() const {
  auto result = db_.Scalar(SafeQuery("SELECT COUNT(*) FROM players"));
  if (!result.IsOk()) return base::Result<int64_t>::Err(result.Error());
  if (!result.Unwrap().has_value()) return base::Result<int64_t>::Ok(0);
  try {
    return base::Result<int64_t>::Ok(std::stoll(*result.Unwrap()));
  } catch (...) {
    return base::Result<int64_t>::Ok(0);
  }
}

base::Result<std::vector<Player>> SafePlayerRepository::GetRecentlyActive(int limit) const {
  auto result = db_.Query(SafeQuery("SELECT id, username, display_name, chips, total_profit, "
                                    "hands_played FROM players ORDER BY created_at DESC LIMIT ?")
                              .Bind(std::max(1, std::min(limit, 100))));

  if (!result.IsOk()) return base::Result<std::vector<Player>>::Err(result.Error());

  std::vector<Player> players;
  for (auto& row : result.Unwrap()) {
    Player p;
    p.player_id = std::stoll(row[0]);
    p.username = row[1];
    p.display_name = row[2];
    p.chips = std::stoll(row[3]);
    p.total_profit = std::stoll(row[4]);
    p.hands_played = std::stoll(row[5]);
    players.push_back(std::move(p));
  }

  return base::Result<std::vector<Player>>::Ok(std::move(players));
}

}  // namespace poker_engine::persistence
