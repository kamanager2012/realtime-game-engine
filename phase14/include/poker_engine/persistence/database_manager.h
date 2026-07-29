#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

// 使用 SQLite 作为默认存储
// 生产环境可通过编译选项切换为 PostgreSQL
#ifdef POKER_ENGINE_USE_POSTGRES
#include <libpq-fe.h>
#else
#include <sqlite3.h>
#endif

namespace poker_engine::persistence {

enum class DBType { SQLite = 0, Postgres = 1 };

class DatabaseManager {
 public:
  explicit DatabaseManager(const std::string& connection_string, DBType type = DBType::SQLite);
  ~DatabaseManager();

  // 连接数据库
  bool Connect();
  void Disconnect();
  bool IsConnected() const;

  // 执行查询（不返回结果）
  bool Execute(const std::string& sql);

  // 执行查询（返回结果集）
  using RowCallback = std::function<bool(const std::vector<std::string>&)>;
  bool Query(const std::string& sql, RowCallback callback);

  // 预编译语句
  bool Prepare(const std::string& sql);
  bool Bind(int index, int64_t value);
  bool Bind(int index, const std::string& value);
  bool Bind(int index, double value);
  bool Step();
  std::string GetColumnText(int column);
  int64_t GetColumnInt64(int column);
  void Reset();
  void Finalize();

  // 迁移
  bool RunMigrations();
  int GetSchemaVersion();

  // 单例访问
  static DatabaseManager& Instance();
  static void SetInstance(std::unique_ptr<DatabaseManager> instance);

 private:
  std::string connection_string_;
  DBType db_type_;
  bool connected_ = false;

#ifdef POKER_ENGINE_USE_POSTGRES
  PGconn* pg_conn_ = nullptr;
#else
  sqlite3* sqlite_db_ = nullptr;
  sqlite3_stmt* stmt_ = nullptr;
#endif

  bool EnsureTables();
};

}  // namespace poker_engine::persistence
