#pragma once
#include <string>
#include <vector>

#include "poker_engine/phase14/database.h"

namespace poker_engine::phase14 {

// ========== 数据库迁移 ==========
// 管理 schema 版本，支持自动升级

struct Migration {
  int version;
  std::string name;
  std::string up_sql;
  std::string down_sql;
};

class MigrationManager {
 public:
  explicit MigrationManager(Database& db);

  // 获取当前schema版本
  int GetCurrentVersion();

  // 运行所有未应用的迁移
  void MigrateToLatest();

  // 迁移到指定版本
  void MigrateTo(int target_version);

  // 注册迁移
  void AddMigration(int version, const std::string& name, const std::string& up_sql,
                    const std::string& down_sql = "");

  // 获取迁移历史
  std::vector<std::pair<int, std::string>> GetMigrationHistory();

 private:
  Database& db_;
  std::vector<Migration> migrations_;

  void EnsureMigrationsTable();
  void RecordMigration(int version, const std::string& name);
};

}  // namespace poker_engine::phase14
