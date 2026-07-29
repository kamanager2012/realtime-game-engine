#include "poker_engine/persistence/safe_database.h"

#include <sqlite3.h>

#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::persistence {

// ==================== SafeDatabase ====================

SafeDatabase::SafeDatabase(const std::string& path) : path_(path) {}

SafeDatabase::~SafeDatabase() { Disconnect(); }

base::Result<void> SafeDatabase::Connect() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (db_) {
    PE_LOG_WARN("Database already connected");
    return base::Result<void>::Ok();
  }

  int rc = sqlite3_open(path_.c_str(), &db_);
  if (rc != SQLITE_OK) {
    std::string err = LastError();
    Disconnect();
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::DatabaseError));
  }

  // 安全设置
  sqlite3_exec(db_, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
  sqlite3_busy_timeout(db_, 5000);  // 5 秒超时

  PE_LOG_INFO("Database connected: {}", path_);
  return base::Result<void>::Ok();
}

void SafeDatabase::Disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
    PE_LOG_INFO("Database disconnected");
  }
}

bool SafeDatabase::IsConnected() const { return db_ != nullptr; }

base::Result<std::vector<std::vector<std::string>>> SafeDatabase::Query(const SafeQuery& query) {
  std::string sql = query.Build();
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return base::Result<std::vector<std::vector<std::string>>>::Err(
        base::MakeErrorCode(base::Error::DatabaseError));

  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    PE_LOG_ERROR("Query prepare failed: {} — SQL: {}", sqlite3_errmsg(db_), sql);
    return base::Result<std::vector<std::vector<std::string>>>::Err(
        base::MakeErrorCode(base::Error::DatabaseError));
  }

  // 确保语句被 finalize
  struct ScopeFinalize {
    sqlite3_stmt*& s;
    ~ScopeFinalize() {
      if (s) sqlite3_finalize(s);
    }
  } guard{stmt};

  std::vector<std::vector<std::string>> results;

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    int cols = sqlite3_column_count(stmt);
    std::vector<std::string> row;
    row.reserve(cols);
    for (int i = 0; i < cols; ++i) {
      const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
      row.push_back(val ? val : "");
    }
    results.push_back(std::move(row));
  }

  if (rc != SQLITE_DONE) {
    PE_LOG_ERROR("Query execution failed: {} — SQL: {}", sqlite3_errmsg(db_), sql);
    return base::Result<std::vector<std::vector<std::string>>>::Err(
        base::MakeErrorCode(base::Error::DatabaseError));
  }

  return base::Result<std::vector<std::vector<std::string>>>::Ok(std::move(results));
}

base::Result<int64_t> SafeDatabase::Execute(const SafeQuery& query) {
  std::string sql = query.Build();
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_) return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::DatabaseError));

  char* errmsg = nullptr;
  int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);

  if (rc != SQLITE_OK) {
    std::string error(errmsg ? errmsg : "unknown");
    sqlite3_free(errmsg);
    PE_LOG_ERROR("Execute failed: {} — SQL: {}", error, sql);
    return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::DatabaseError));
  }

  return base::Result<int64_t>::Ok(sqlite3_last_insert_rowid(db_));
}

base::Result<std::optional<std::string>> SafeDatabase::Scalar(const SafeQuery& query) {
  auto result = Query(query);
  if (!result.IsOk()) {
    return base::Result<std::optional<std::string>>::Err(result.Error());
  }
  auto& rows = result.Unwrap();
  if (rows.empty() || rows[0].empty()) {
    return base::Result<std::optional<std::string>>::Ok(std::nullopt);
  }
  return base::Result<std::optional<std::string>>::Ok(rows[0][0]);
}

// ==================== Transaction ====================

SafeDatabase::Transaction SafeDatabase::BeginTransaction() { return Transaction(*this); }

SafeDatabase::Transaction::Transaction(SafeDatabase& db) : db_(db) {
  db_.Execute(SafeQuery("BEGIN TRANSACTION"));
  active_ = true;
}

SafeDatabase::Transaction::~Transaction() {
  if (active_ && !committed_) {
    Rollback();
  }
}

base::Result<void> SafeDatabase::Transaction::Commit() {
  if (!active_) return base::Result<void>::Err(base::MakeErrorCode(base::Error::InvalidArgument));
  auto result = db_.Execute(SafeQuery("COMMIT"));
  if (result.IsOk()) {
    committed_ = true;
    active_ = false;
    return base::Result<void>::Ok();
  }
  return base::Result<void>::Err(result.Error());
}

void SafeDatabase::Transaction::Rollback() {
  if (active_) {
    db_.Execute(SafeQuery("ROLLBACK"));
    active_ = false;
  }
}

base::Result<std::vector<std::vector<std::string>>> SafeDatabase::Transaction::Query(
    const SafeQuery& query) {
  return db_.Query(query);
}

base::Result<int64_t> SafeDatabase::Transaction::Execute(const SafeQuery& query) {
  return db_.Execute(query);
}

// ==================== PreparedStatement ====================

SafeDatabase::PreparedStatement::PreparedStatement(sqlite3_stmt* stmt) : stmt_(stmt) {}

SafeDatabase::PreparedStatement::~PreparedStatement() {
  if (stmt_) sqlite3_finalize(stmt_);
}

SafeDatabase::PreparedStatement::PreparedStatement(PreparedStatement&& other) noexcept
    : stmt_(other.stmt_) {
  other.stmt_ = nullptr;
}

SafeDatabase::PreparedStatement& SafeDatabase::PreparedStatement::operator=(
    PreparedStatement&& other) noexcept {
  if (this != &other) {
    if (stmt_) sqlite3_finalize(stmt_);
    stmt_ = other.stmt_;
    other.stmt_ = nullptr;
  }
  return *this;
}

void SafeDatabase::PreparedStatement::Bind(int pos, int value) {
  sqlite3_bind_int(stmt_, pos + 1, value);
}

void SafeDatabase::PreparedStatement::Bind(int pos, int64_t value) {
  sqlite3_bind_int64(stmt_, pos + 1, value);
}

void SafeDatabase::PreparedStatement::Bind(int pos, double value) {
  sqlite3_bind_double(stmt_, pos + 1, value);
}

void SafeDatabase::PreparedStatement::Bind(int pos, const std::string& value) {
  sqlite3_bind_text(stmt_, pos + 1, value.c_str(), -1, SQLITE_TRANSIENT);
}

void SafeDatabase::PreparedStatement::Bind(int pos, const char* value) {
  sqlite3_bind_text(stmt_, pos + 1, value, -1, SQLITE_TRANSIENT);
}

void SafeDatabase::PreparedStatement::BindNull(int pos) { sqlite3_bind_null(stmt_, pos + 1); }

bool SafeDatabase::PreparedStatement::Step() { return sqlite3_step(stmt_) == SQLITE_ROW; }

bool SafeDatabase::PreparedStatement::Exec() {
  int rc = sqlite3_step(stmt_);
  return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

int SafeDatabase::PreparedStatement::GetInt(int col) const {
  return sqlite3_column_int(stmt_, col);
}

int64_t SafeDatabase::PreparedStatement::GetInt64(int col) const {
  return sqlite3_column_int64(stmt_, col);
}

double SafeDatabase::PreparedStatement::GetDouble(int col) const {
  return sqlite3_column_double(stmt_, col);
}

std::string SafeDatabase::PreparedStatement::GetString(int col) const {
  const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, col));
  return val ? val : "";
}

bool SafeDatabase::PreparedStatement::IsNull(int col) const {
  return sqlite3_column_type(stmt_, col) == SQLITE_NULL;
}

void SafeDatabase::PreparedStatement::Reset() { sqlite3_reset(stmt_); }

void SafeDatabase::PreparedStatement::ClearBindings() { sqlite3_clear_bindings(stmt_); }

base::Result<SafeDatabase::PreparedStatement> SafeDatabase::Prepare(const std::string& sql) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return base::Result<PreparedStatement>::Err(base::MakeErrorCode(base::Error::DatabaseError));

  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    PE_LOG_ERROR("Prepare failed: {} — SQL: {}", sqlite3_errmsg(db_), sql);
    return base::Result<PreparedStatement>::Err(base::MakeErrorCode(base::Error::DatabaseError));
  }
  return base::Result<PreparedStatement>::Ok(PreparedStatement(stmt));
}

base::Result<void> SafeDatabase::ExecuteMigration(int version, const std::string& sql) {
  auto txn = BeginTransaction();
  auto result = Execute(SafeQuery(sql));
  if (!result.IsOk()) {
    txn.Rollback();
    return base::Result<void>::Err(result.Error());
  }
  // Record migration version
  Execute(
      SafeQuery(
          "INSERT OR REPLACE INTO schema_version (version, applied_at) VALUES (?, datetime('now'))")
          .Bind(version));
  txn.Commit();
  return base::Result<void>::Ok();
}

int SafeDatabase::GetCurrentVersion() {
  auto result = Scalar(SafeQuery("SELECT MAX(version) FROM schema_version"));
  if (!result.IsOk() || !result.Unwrap().has_value()) return 0;
  try {
    return std::stoi(result.Unwrap().value());
  } catch (...) {
    return 0;
  }
}

std::string SafeDatabase::LastError() const { return db_ ? sqlite3_errmsg(db_) : "not connected"; }

}  // namespace poker_engine::persistence
