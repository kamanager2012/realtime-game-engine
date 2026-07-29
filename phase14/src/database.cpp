#include "poker_engine/phase14/database.h"

#include <iostream>
#include <sstream>
#include <stdexcept>

#include "poker_engine/base/logging.h"

namespace poker_engine::phase14 {

// ===================== Row =====================
Row::Row(sqlite3_stmt* stmt) : stmt_(stmt) { col_count_ = sqlite3_column_count(stmt); }

int Row::GetInt(int col) const { return sqlite3_column_int(stmt_, col); }

int64_t Row::GetInt64(int col) const { return sqlite3_column_int64(stmt_, col); }

double Row::GetDouble(int col) const { return sqlite3_column_double(stmt_, col); }

std::string Row::GetString(int col) const {
  const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, col));
  return text ? std::string(text) : "";
}

bool Row::IsNull(int col) const { return sqlite3_column_type(stmt_, col) == SQLITE_NULL; }

// ===================== ResultSet =====================
ResultSet::ResultSet() = default;

ResultSet::ResultSet(sqlite3_stmt* stmt) : stmt_(stmt), done_(false) {
  if (stmt_) {
    last_row_id_ = sqlite3_last_insert_rowid(sqlite3_db_handle(stmt_));
    rows_affected_ = sqlite3_changes(sqlite3_db_handle(stmt_));
  }
}

ResultSet::~ResultSet() {
  if (stmt_) sqlite3_finalize(stmt_);
}

ResultSet::ResultSet(ResultSet&& other) noexcept
    : stmt_(other.stmt_),
      done_(other.done_),
      last_row_id_(other.last_row_id_),
      rows_affected_(other.rows_affected_) {
  other.stmt_ = nullptr;
}

ResultSet& ResultSet::operator=(ResultSet&& other) noexcept {
  if (this != &other) {
    if (stmt_) sqlite3_finalize(stmt_);
    stmt_ = other.stmt_;
    done_ = other.done_;
    last_row_id_ = other.last_row_id_;
    rows_affected_ = other.rows_affected_;
    other.stmt_ = nullptr;
  }
  return *this;
}

bool ResultSet::Next() {
  if (!stmt_) return false;
  int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) {
    done_ = false;
    return true;
  }
  done_ = true;
  return false;
}

Row ResultSet::GetRow() const { return Row(stmt_); }

int ResultSet::ColumnCount() const { return stmt_ ? sqlite3_column_count(stmt_) : 0; }

std::string ResultSet::ColumnName(int col) const {
  return stmt_ ? sqlite3_column_name(stmt_, col) : "";
}

std::string ResultSet::ColumnType(int col) const {
  if (!stmt_) return "";
  switch (sqlite3_column_type(stmt_, col)) {
    case SQLITE_INTEGER:
      return "INTEGER";
    case SQLITE_FLOAT:
      return "REAL";
    case SQLITE_TEXT:
      return "TEXT";
    case SQLITE_BLOB:
      return "BLOB";
    case SQLITE_NULL:
      return "NULL";
    default:
      return "UNKNOWN";
  }
}

int64_t ResultSet::LastRowId() const { return last_row_id_; }
int ResultSet::RowsAffected() const { return rows_affected_; }
void ResultSet::Reset() {
  if (stmt_) sqlite3_reset(stmt_);
  done_ = false;
}

// ===================== Database =====================

Database::Database(const std::string& filepath) { Open(filepath); }

Database::~Database() { Close(); }

Database::Database(Database&& other) noexcept
    : db_(other.db_),
      filepath_(std::move(other.filepath_)),
      in_transaction_(other.in_transaction_) {
  other.db_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
  if (this != &other) {
    Close();
    db_ = other.db_;
    filepath_ = std::move(other.filepath_);
    in_transaction_ = other.in_transaction_;
    other.db_ = nullptr;
  }
  return *this;
}

bool Database::Open(const std::string& path, bool read_only) {
  if (db_) Close();

  int flags = read_only ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
  int rc = sqlite3_open_v2(path.c_str(), &db_, flags, nullptr);

  if (rc != SQLITE_OK) {
    std::cerr << "Failed to open database: " << sqlite3_errmsg(db_) << "\n";
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  filepath_ = path;
  EnableJournalMode("WAL");
  EnableForeignKey(true);

  return true;
}

void Database::Close() {
  if (in_transaction_) {
    try {
      Rollback();
    } catch (const std::exception& e) {
      PE_LOG_ERROR("Database::Close: rollback failed during close: {}", e.what());
    } catch (...) {
      PE_LOG_ERROR("Database::Close: unknown error during rollback on close");
    }
  }
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool Database::IsOpen() const { return db_ != nullptr; }

bool Database::Execute(const std::string& sql) {
  std::string err;
  return Execute(sql, err);
}

bool Database::Execute(const std::string& sql, std::string& error_msg) {
  if (!db_) {
    error_msg = "Database not open";
    return false;
  }

  char* err = nullptr;
  int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);

  if (rc != SQLITE_OK) {
    error_msg = err ? std::string(err) : "Unknown error";
    sqlite3_free(err);
    return false;
  }
  return true;
}

ResultSet Database::Query(const std::string& sql) {
  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    std::cerr << "Query prepare failed: " << sqlite3_errmsg(db_) << "\n";
    return ResultSet();
  }
  return ResultSet(stmt);
}

ResultSet Database::Prepare(const std::string& sql) { return Query(sql); }

void Database::BeginTransaction() {
  if (!in_transaction_) {
    Execute("BEGIN TRANSACTION");
    in_transaction_ = true;
  }
}

void Database::Commit() {
  if (in_transaction_) {
    Execute("COMMIT");
    in_transaction_ = false;
  }
}

void Database::Rollback() {
  if (in_transaction_) {
    Execute("ROLLBACK");
    in_transaction_ = false;
  }
}

bool Database::InTransaction() const { return in_transaction_; }

int64_t Database::LastInsertRowId() const { return db_ ? sqlite3_last_insert_rowid(db_) : -1; }

int64_t Database::RowsAffected() const { return db_ ? sqlite3_changes(db_) : 0; }

std::string Database::GetVersion() const { return sqlite3_libversion(); }

std::string Database::GetFilePath() const { return filepath_; }

void Database::EnableJournalMode(const std::string& mode) {
  std::string sql = "PRAGMA journal_mode=" + mode;
  Execute(sql);
}

void Database::EnableForeignKey(bool enable) {
  Execute(enable ? "PRAGMA foreign_keys=ON" : "PRAGMA foreign_keys=OFF");
}

void Database::CheckError(int rc, const std::string& context) {
  if (rc != SQLITE_OK && db_) {
    throw std::runtime_error(context + ": " + sqlite3_errmsg(db_));
  }
}

// ===================== ScopedTransaction =====================

ScopedTransaction::ScopedTransaction(Database& db) : db_(db) { db_.BeginTransaction(); }

ScopedTransaction::~ScopedTransaction() {
  if (!committed_ && !rolled_back_) {
    try {
      Rollback();
    } catch (const std::exception& e) {
      PE_LOG_ERROR("ScopedTransaction: rollback failed in destructor: {}", e.what());
    } catch (...) {
      PE_LOG_ERROR("ScopedTransaction: unknown error during rollback in destructor");
    }
  }
}

void ScopedTransaction::Commit() {
  if (!committed_ && !rolled_back_) {
    db_.Commit();
    committed_ = true;
  }
}

void ScopedTransaction::Rollback() {
  if (!committed_ && !rolled_back_) {
    db_.Rollback();
    rolled_back_ = true;
  }
}

// ===================== StatementBinder =====================

StatementBinder::StatementBinder(ResultSet& result) : result_(result) {}

StatementBinder& StatementBinder::Bind(int index, int value) {
  sqlite3_bind_int(result_.stmt_, index, value);
  return *this;
}

StatementBinder& StatementBinder::Bind(int index, int64_t value) {
  sqlite3_bind_int64(result_.stmt_, index, value);
  return *this;
}

StatementBinder& StatementBinder::Bind(int index, double value) {
  sqlite3_bind_double(result_.stmt_, index, value);
  return *this;
}

StatementBinder& StatementBinder::Bind(int index, const std::string& value) {
  sqlite3_bind_text(result_.stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
  return *this;
}

StatementBinder& StatementBinder::Bind(int index, const char* value) {
  sqlite3_bind_text(result_.stmt_, index, value, -1, SQLITE_TRANSIENT);
  return *this;
}

StatementBinder& StatementBinder::BindNull(int index) {
  sqlite3_bind_null(result_.stmt_, index);
  return *this;
}

}  // namespace poker_engine::phase14
