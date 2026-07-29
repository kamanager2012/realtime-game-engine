#pragma once
#include <sqlite3.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace poker_engine::phase14 {

// ========== 查询结果行 ==========
class Row {
 public:
  explicit Row(sqlite3_stmt* stmt);

  int GetInt(int col) const;
  int64_t GetInt64(int col) const;
  double GetDouble(int col) const;
  std::string GetString(int col) const;
  bool IsNull(int col) const;

 private:
  sqlite3_stmt* stmt_;
  int col_count_;
};

// ========== 查询结果集 ==========
class ResultSet {
 public:
  ResultSet();
  explicit ResultSet(sqlite3_stmt* stmt);
  ~ResultSet();

  ResultSet(ResultSet&& other) noexcept;
  ResultSet& operator=(ResultSet&& other) noexcept;
  ResultSet(const ResultSet&) = delete;
  ResultSet& operator=(const ResultSet&) = delete;

  bool Next();
  Row GetRow() const;
  int ColumnCount() const;
  std::string ColumnName(int col) const;
  std::string ColumnType(int col) const;
  int64_t LastRowId() const;
  int RowsAffected() const;
  void Reset();

  // Access for StatementBinder
  sqlite3_stmt* stmt_ = nullptr;

 private:
  bool done_ = true;
  int64_t last_row_id_ = -1;
  int rows_affected_ = 0;
};

// ========== 数据库连接 ==========
class Database {
 public:
  explicit Database(const std::string& filepath = ":memory:");
  ~Database();

  // 禁用拷贝
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  // 启用移动
  Database(Database&&) noexcept;
  Database& operator=(Database&&) noexcept;

  // 打开/关闭连接
  bool Open(const std::string& filepath, bool read_only = false);
  void Close();
  bool IsOpen() const;

  // 执行SQL (不返回结果)
  bool Execute(const std::string& sql);
  bool Execute(const std::string& sql, std::string& error_msg);

  // 执行查询 (返回结果集)
  ResultSet Query(const std::string& sql);

  // 预编译语句
  ResultSet Prepare(const std::string& sql);

  // 事务
  void BeginTransaction();
  void Commit();
  void Rollback();
  bool InTransaction() const;

  // 工具
  int64_t LastInsertRowId() const;
  int64_t RowsAffected() const;
  std::string GetVersion() const;
  std::string GetFilePath() const;

  // 性能
  void EnableJournalMode(const std::string& mode = "WAL");
  void EnableForeignKey(bool enable = true);

  // 原始指针 (高级用法)
  sqlite3* GetHandle() { return db_; }

 private:
  sqlite3* db_ = nullptr;
  std::string filepath_;
  bool in_transaction_ = false;
  std::mutex mutex_;

  void CheckError(int rc, const std::string& context);
};

// ========== 作用域事务 (RAII) ==========
class ScopedTransaction {
 public:
  explicit ScopedTransaction(Database& db);
  ~ScopedTransaction();

  void Commit();
  void Rollback();

 private:
  Database& db_;
  bool committed_ = false;
  bool rolled_back_ = false;
};

// ========== 预编译语句绑定器 ==========
class StatementBinder {
 public:
  StatementBinder(ResultSet& result);

  StatementBinder& Bind(int index, int value);
  StatementBinder& Bind(int index, int64_t value);
  StatementBinder& Bind(int index, double value);
  StatementBinder& Bind(int index, const std::string& value);
  StatementBinder& Bind(int index, const char* value);
  StatementBinder& BindNull(int index);

 private:
  ResultSet& result_;
};

}  // namespace poker_engine::phase14
