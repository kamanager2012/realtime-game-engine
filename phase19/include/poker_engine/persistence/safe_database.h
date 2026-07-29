#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "poker_engine/base/result.h"
#include "safe_query.h"

struct sqlite3;
struct sqlite3_stmt;

namespace poker_engine::persistence {

// ==================== 安全数据库连接 ====================
// 所有查询必须通过 SafeQuery 构建，防止 SQL 注入

class SafeDatabase {
 public:
  explicit SafeDatabase(const std::string& path);
  ~SafeDatabase();

  // 连接/断开
  base::Result<void> Connect();
  void Disconnect();
  bool IsConnected() const;

  // 执行查询（返回多行）
  base::Result<std::vector<std::vector<std::string>>> Query(const SafeQuery& query);

  // 执行语句（INSERT/UPDATE/DELETE）
  base::Result<int64_t> Execute(const SafeQuery& query);

  // 标量查询
  base::Result<std::optional<std::string>> Scalar(const SafeQuery& query);

  // 事务
  class Transaction {
   public:
    explicit Transaction(SafeDatabase& db);
    ~Transaction();

    base::Result<void> Commit();
    void Rollback();

    base::Result<std::vector<std::vector<std::string>>> Query(const SafeQuery& query);
    base::Result<int64_t> Execute(const SafeQuery& query);

   private:
    SafeDatabase& db_;
    bool committed_ = false;
    bool active_ = false;
  };

  Transaction BeginTransaction();

  // 预编译语句
  class PreparedStatement {
   public:
    PreparedStatement() = default;
    ~PreparedStatement();

    PreparedStatement(const PreparedStatement&) = delete;
    PreparedStatement& operator=(const PreparedStatement&) = delete;
    PreparedStatement(PreparedStatement&& other) noexcept;
    PreparedStatement& operator=(PreparedStatement&& other) noexcept;

    void Bind(int pos, int value);
    void Bind(int pos, int64_t value);
    void Bind(int pos, double value);
    void Bind(int pos, const std::string& value);
    void Bind(int pos, const char* value);
    void BindNull(int pos);

    bool Step();
    bool Exec();  // INSERT/UPDATE/DELETE: returns true on SQLITE_DONE or SQLITE_ROW
    int GetInt(int col) const;
    int64_t GetInt64(int col) const;
    double GetDouble(int col) const;
    std::string GetString(int col) const;
    bool IsNull(int col) const;

    void Reset();
    void ClearBindings();

   private:
    friend class SafeDatabase;
    explicit PreparedStatement(sqlite3_stmt* stmt);
    sqlite3_stmt* stmt_ = nullptr;
  };

  base::Result<PreparedStatement> Prepare(const std::string& sql);

  // 迁移支持
  base::Result<void> ExecuteMigration(int version, const std::string& sql);
  int GetCurrentVersion();

 private:
  std::string path_;
  sqlite3* db_ = nullptr;
  mutable std::mutex mutex_;

  std::string LastError() const;
};

}  // namespace poker_engine::persistence
