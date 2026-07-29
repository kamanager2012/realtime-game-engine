#pragma once
#include <memory>
#include <string>

#include "poker_engine/phase14/database.h"
#include "poker_engine/phase14/hand_repository.h"
#include "poker_engine/phase14/migration.h"
#include "poker_engine/phase14/player_repository.h"
#include "poker_engine/phase14/account_repository.h"
#include "poker_engine/phase14/chip_ledger.h"
#include "poker_engine/phase14/postgres_mirror.h"
#include "poker_engine/phase14/query_builder.h"
#include "poker_engine/phase14/stat_repository.h"

namespace poker_engine::phase14 {

// ========== 数据库管理器 (facade) ==========
class DatabaseManager {
 public:
  static DatabaseManager& Instance();

  bool Initialize(const std::string& db_path = "poker_engine.db");
  void Shutdown();
  bool IsHealthy() const;

  // === 子系统访问 ===
  HandRepository& GetHandRepo();
  PlayerRepository& GetPlayerRepo();
  StatRepository& GetStatRepo();
  Database& GetDatabase();
  AccountRepository& GetAccountRepo();
  ChipLedger& GetChipLedger();

  bool ConnectPostgresMirror(const std::string& url);
  void DisconnectPostgresMirror();
  bool IsPostgresMirrorConnected() const;

  // === 高级操作 ===
  std::string GetPlayerReport(int32_t player_id);
  std::string GetLeaderboardJSON(int limit = 20);
  std::string GetDatabaseStats();
  bool Backup(const std::string& backup_path);

 private:
  DatabaseManager() = default;
  ~DatabaseManager() = default;

  DatabaseManager(const DatabaseManager&) = delete;
  DatabaseManager& operator=(const DatabaseManager&) = delete;

  std::unique_ptr<Database> db_;
  std::unique_ptr<HandRepository> hand_repo_;
  std::unique_ptr<PlayerRepository> player_repo_;
  std::unique_ptr<StatRepository> stat_repo_;
  std::unique_ptr<AccountRepository> account_repo_;
  std::unique_ptr<ChipLedger> chip_ledger_;
  std::unique_ptr<PostgresMirror> postgres_mirror_;
  bool initialized_ = false;
};

}  // namespace poker_engine::phase14
