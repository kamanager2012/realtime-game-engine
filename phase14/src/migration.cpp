#include "poker_engine/phase14/migration.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace poker_engine::phase14 {

MigrationManager::MigrationManager(Database& db) : db_(db) {}

void MigrationManager::EnsureMigrationsTable() {
  db_.Execute(R"(
        CREATE TABLE IF NOT EXISTS schema_migrations (
            version INTEGER PRIMARY KEY,
            name TEXT NOT NULL,
            applied_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )");
}

int MigrationManager::GetCurrentVersion() {
  EnsureMigrationsTable();

  auto rs = db_.Query("SELECT MAX(version) FROM schema_migrations");
  if (rs.Next()) {
    Row row = rs.GetRow();
    if (!row.IsNull(0)) return row.GetInt(0);
  }
  return 0;
}

void MigrationManager::AddMigration(int version, const std::string& name, const std::string& up_sql,
                                    const std::string& down_sql) {
  migrations_.push_back({version, name, up_sql, down_sql});
}

void MigrationManager::MigrateToLatest() {
  EnsureMigrationsTable();

  std::sort(migrations_.begin(), migrations_.end(),
            [](const Migration& a, const Migration& b) { return a.version < b.version; });

  int current = GetCurrentVersion();
  int count = 0;

  for (const auto& m : migrations_) {
    if (m.version > current) {
      std::cout << "  Migrating v" << current << " -> v" << m.version << ": " << m.name << "...\n";

      ScopedTransaction tx(db_);

      std::string sql = m.up_sql;
      size_t pos = 0;
      while (pos < sql.size()) {
        size_t end = sql.find(';', pos);
        if (end == std::string::npos) {
          std::string statement = sql.substr(pos);
          while (!statement.empty() && (statement.front() == ' ' || statement.front() == '\n' ||
                                        statement.front() == '\r'))
            statement.erase(statement.begin());
          while (!statement.empty() &&
                 (statement.back() == ' ' || statement.back() == '\n' || statement.back() == '\r'))
            statement.pop_back();
          if (!statement.empty()) {
            std::string err;
            if (!db_.Execute(statement, err)) {
              std::cerr << "Migration SQL error: " << err << "\n";
            }
          }
          break;
        }
        std::string statement = sql.substr(pos, end - pos);
        while (!statement.empty() &&
               (statement.front() == ' ' || statement.front() == '\n' || statement.front() == '\r'))
          statement.erase(statement.begin());
        while (!statement.empty() &&
               (statement.back() == ' ' || statement.back() == '\n' || statement.back() == '\r'))
          statement.pop_back();
        if (!statement.empty()) {
          std::string err;
          if (!db_.Execute(statement, err)) {
            std::cerr << "Migration SQL error: " << err << "\n";
          }
        }
        pos = end + 1;
      }

      RecordMigration(m.version, m.name);
      tx.Commit();
      current = m.version;
      count++;
    }
  }

  std::cout << "  Migration complete. Applied " << count << " migration(s).\n";
}

void MigrationManager::MigrateTo(int target_version) { MigrateToLatest(); }

void MigrationManager::RecordMigration(int version, const std::string& name) {
  std::string escaped = name;
  for (size_t i = 0; i < escaped.size(); i++) {
    if (escaped[i] == '\'') {
      escaped.insert(i, 1, '\'');
      i++;
    }
  }
  db_.Execute("INSERT INTO schema_migrations (version, name) VALUES (" + std::to_string(version) +
              ", '" + escaped + "')");
}

std::vector<std::pair<int, std::string>> MigrationManager::GetMigrationHistory() {
  EnsureMigrationsTable();
  std::vector<std::pair<int, std::string>> history;

  auto rs = db_.Query("SELECT version, name, applied_at FROM schema_migrations ORDER BY version");
  while (rs.Next()) {
    Row row = rs.GetRow();
    history.push_back({row.GetInt(0), row.GetString(1)});
  }
  return history;
}

}  // namespace poker_engine::phase14
