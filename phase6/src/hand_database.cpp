#include "poker_engine/phase6/hand_database.h"

#include <iomanip>
#include <sstream>

namespace poker_engine {
namespace phase6 {

#ifdef HAS_SQLITE3

// ===================== SQLite3 实现 =====================

HandDatabase::HandDatabase() = default;
HandDatabase::~HandDatabase() { Close(); }

bool HandDatabase::Open(const std::string& db_path) {
  if (db_) Close();
  db_path_ = db_path;
  int rc = sqlite3_open(db_path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }
  sqlite3_exec(db_, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
  return true;
}

void HandDatabase::Close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool HandDatabase::IsOpen() const { return db_ != nullptr; }

bool HandDatabase::Execute(const std::string& sql, std::string* error) {
  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    last_error_ = err_msg ? err_msg : "Unknown SQLite error";
    sqlite3_free(err_msg);
    if (error) *error = last_error_;
    return false;
  }
  return true;
}

int HandDatabase::Callback(void* data, int cols, char** values, char** names) {
  auto* rows = static_cast<std::vector<std::map<std::string, std::string>>*>(data);
  std::map<std::string, std::string> row;
  for (int i = 0; i < cols; i++) {
    row[names[i]] = values[i] ? values[i] : "";
  }
  rows->push_back(row);
  return 0;
}

bool HandDatabase::CreateSchema() {
  std::string sql = R"(
        CREATE TABLE IF NOT EXISTS hands (
            hand_id INTEGER PRIMARY KEY,
            site TEXT,
            game_type TEXT,
            table_name TEXT,
            hero_name TEXT,
            hero_cards TEXT,
            board TEXT,
            big_blind REAL,
            small_blind REAL,
            total_pot REAL,
            hero_net REAL,
            result TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS hands_actions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            hand_id INTEGER,
            street INTEGER,
            player_name TEXT,
            action TEXT,
            amount REAL,
            FOREIGN KEY (hand_id) REFERENCES hands(hand_id)
        );

        CREATE TABLE IF NOT EXISTS sessions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT,
            start_time DATETIME,
            end_time DATETIME
        );

        CREATE INDEX IF NOT EXISTS idx_hands_hero ON hands(hero_name);
        CREATE INDEX IF NOT EXISTS idx_hands_site ON hands(site);
        CREATE INDEX IF NOT EXISTS idx_hands_date ON hands(timestamp);
        CREATE INDEX IF NOT EXISTS idx_actions_hand ON hands_actions(hand_id);
    )";
  return Execute(sql);
}

bool HandDatabase::Migrate(int) { return true; }

bool HandDatabase::InsertHand(const poker_engine::phase4::HandHistory& hh) {
  if (!db_) return false;

  auto hero_cards = hh.HeroCards();
  std::string hc_str;
  if (hero_cards.size() >= 2) {
    hc_str = hero_cards[0].ToString() + " " + hero_cards[1].ToString();
  }

  std::string board_str = hh.BoardString();

  double hero_net = 0;
  std::string result;
  for (const auto& r : hh.results) {
    if (r.player_name == hh.HeroName()) {
      hero_net += r.amount;
      result = "Win $" + std::to_string((int)r.amount);
    }
  }
  if (result.empty()) {
    hero_net = -hh.total_pot * 0.5;
    result = "Lose";
  }

  // Escape single quotes for SQL
  auto esc = [](const std::string& s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
      if (c == '\'')
        out += "''";
      else
        out += c;
    }
    return out;
  };

  std::ostringstream sql;
  sql << std::fixed << std::setprecision(2);
  sql << "INSERT OR REPLACE INTO hands (hand_id, site, game_type, table_name, hero_name, "
         "hero_cards, board, big_blind, small_blind, total_pot, hero_net, result) VALUES ("
      << hh.hand_id << ", "
      << "'" << esc(hh.site) << "', "
      << "'" << esc(hh.game_type) << "', "
      << "'" << esc(hh.table_name) << "', "
      << "'" << esc(hh.HeroName()) << "', "
      << "'" << esc(hc_str) << "', "
      << "'" << esc(board_str) << "', " << hh.big_blind << ", " << hh.small_blind << ", "
      << hh.total_pot << ", " << hero_net << ", "
      << "'" << esc(result) << "'"
      << ");";

  if (!Execute(sql.str())) return false;

  for (const auto& sr : hh.streets) {
    for (const auto& a : sr.actions) {
      std::ostringstream asql;
      asql << "INSERT INTO hands_actions (hand_id, street, player_name, action, amount) VALUES ("
           << hh.hand_id << ", " << static_cast<int>(sr.street) << ", "
           << "'" << a.player_name << "', "
           << "'" << static_cast<int>(a.action) << "', " << a.amount << ");";
      Execute(asql.str());
    }
  }
  return true;
}

bool HandDatabase::InsertHands(const std::vector<poker_engine::phase4::HandHistory>& hands) {
  if (!db_) return false;
  Execute("BEGIN TRANSACTION");
  int count = 0;
  for (const auto& hh : hands) {
    if (InsertHand(hh)) count++;
  }
  Execute("COMMIT");
  return count == static_cast<int>(hands.size());
}

std::vector<DBHandInfo> HandDatabase::QueryHands(const DBQueryOptions& opts) const {
  std::vector<DBHandInfo> results;
  if (!db_) return results;

  std::ostringstream sql;
  sql << "SELECT h.hand_id, h.site, h.game_type, h.hero_name, h.hero_cards, h.board, "
      << "h.big_blind, h.total_pot, h.hero_net, h.result, h.timestamp "
      << "FROM hands h WHERE 1=1";

  if (!opts.player_filter.empty()) sql << " AND h.hero_name LIKE '%" << opts.player_filter << "%'";
  if (!opts.site_filter.empty()) sql << " AND h.site = '" << opts.site_filter << "'";
  if (!opts.game_type_filter.empty())
    sql << " AND h.game_type LIKE '%" << opts.game_type_filter << "%'";
  if (opts.min_bb >= 0) sql << " AND h.big_blind >= " << opts.min_bb;
  if (opts.max_bb >= 0) sql << " AND h.big_blind <= " << opts.max_bb;
  if (!opts.date_from.empty()) sql << " AND h.timestamp >= '" << opts.date_from << "'";
  if (!opts.date_to.empty()) sql << " AND h.timestamp <= '" << opts.date_to << "'";

  sql << " ORDER BY h.hand_id DESC LIMIT " << opts.limit;
  if (opts.offset > 0) sql << " OFFSET " << opts.offset;
  sql << ";";

  char* err = nullptr;
  std::vector<std::map<std::string, std::string>> rows;
  int rc = sqlite3_exec(db_, sql.str().c_str(), Callback, &rows, &err);

  if (rc != SQLITE_OK) {
    last_error_ = err ? err : "Query failed";
    sqlite3_free(err);
    return results;
  }

  for (const auto& row : rows) {
    DBHandInfo info;
    info.hand_id = std::stoll(row.at("hand_id"));
    info.site = row.at("site");
    info.game_type = row.at("game_type");
    info.hero_name = row.at("hero_name");
    info.hero_cards = row.at("hero_cards");
    info.board = row.at("board");
    info.big_blind = std::stod(row.at("big_blind"));
    info.total_pot = std::stod(row.at("total_pot"));
    info.hero_net = std::stod(row.at("hero_net"));
    info.result = row.at("result");
    info.timestamp = row.at("timestamp");
    results.push_back(info);
  }
  return results;
}

std::vector<DBHandInfo> HandDatabase::GetHandHistory(int64_t hand_id) const {
  DBQueryOptions opts;
  opts.limit = 1;
  std::ostringstream sql;
  sql << "SELECT h.hand_id, h.site, h.game_type, h.hero_name, h.hero_cards, h.board, "
      << "h.big_blind, h.total_pot, h.hero_net, h.result, h.timestamp "
      << "FROM hands h WHERE h.hand_id = " << hand_id << ";";

  std::vector<DBHandInfo> results;
  if (!db_) return results;

  char* err = nullptr;
  std::vector<std::map<std::string, std::string>> rows;
  int rc = sqlite3_exec(db_, sql.str().c_str(), Callback, &rows, &err);

  if (rc != SQLITE_OK) {
    last_error_ = err ? err : "Query failed";
    sqlite3_free(err);
    return results;
  }

  for (const auto& row : rows) {
    DBHandInfo info;
    info.hand_id = std::stoll(row.at("hand_id"));
    info.site = row.at("site");
    info.game_type = row.at("game_type");
    info.hero_name = row.at("hero_name");
    info.hero_cards = row.at("hero_cards");
    info.board = row.at("board");
    info.big_blind = std::stod(row.at("big_blind"));
    info.total_pot = std::stod(row.at("total_pot"));
    info.hero_net = std::stod(row.at("hero_net"));
    info.result = row.at("result");
    info.timestamp = row.at("timestamp");
    results.push_back(info);
  }
  return results;
}

int64_t HandDatabase::TotalHands() const {
  if (!db_) return 0;
  std::vector<std::map<std::string, std::string>> rows;
  sqlite3_exec(db_, "SELECT COUNT(*) as cnt FROM hands", Callback, &rows, nullptr);
  if (!rows.empty()) return std::stoll(rows[0]["cnt"]);
  return 0;
}

AggResult HandDatabase::GetAggregates(const DBQueryOptions& opts) const {
  AggResult agg;
  if (!db_) return agg;

  std::ostringstream sql;
  sql << std::fixed << std::setprecision(2);
  sql << "SELECT COUNT(*) as cnt, COALESCE(SUM(total_pot),0) as total_pot, "
      << "COALESCE(AVG(hero_net),0) as avg_net, "
      << "COALESCE(SUM(hero_net),0) as total_net "
      << "FROM hands WHERE 1=1";

  if (!opts.player_filter.empty()) sql << " AND hero_name LIKE '%" << opts.player_filter << "%'";
  if (!opts.site_filter.empty()) sql << " AND site = '" << opts.site_filter << "'";
  if (opts.min_bb >= 0) sql << " AND big_blind >= " << opts.min_bb;
  if (opts.max_bb >= 0) sql << " AND big_blind <= " << opts.max_bb;

  std::vector<std::map<std::string, std::string>> rows;
  char* err = nullptr;
  sqlite3_exec(db_, sql.str().c_str(), Callback, &rows, &err);
  if (err) sqlite3_free(err);

  if (!rows.empty()) {
    agg.hand_count = std::stoll(rows[0]["cnt"]);
    agg.avg_pot = std::stod(rows[0]["total_pot"]);
    agg.total_net = std::stod(rows[0]["total_net"]);
    if (agg.hand_count > 0 && agg.avg_pot > 0) {
      agg.avg_bb100 = (agg.total_net / agg.avg_pot) * 100.0;
      agg.win_rate = (agg.total_net > 0) ? 50.0 + (agg.total_net / agg.avg_pot) * 50.0
                                         : 50.0 - std::abs(agg.total_net / agg.avg_pot) * 50.0;
    }
  }
  return agg;
}

std::vector<std::pair<std::string, int64_t>> HandDatabase::GetSites() const {
  std::vector<std::pair<std::string, int64_t>> results;
  if (!db_) return results;

  std::vector<std::map<std::string, std::string>> rows;
  sqlite3_exec(db_, "SELECT site, COUNT(*) as cnt FROM hands GROUP BY site ORDER BY cnt DESC",
               Callback, &rows, nullptr);
  for (const auto& row : rows) results.push_back({row.at("site"), std::stoll(row.at("cnt"))});
  return results;
}

std::vector<std::pair<std::string, int64_t>> HandDatabase::GetPlayers() const {
  std::vector<std::pair<std::string, int64_t>> results;
  if (!db_) return results;

  std::vector<std::map<std::string, std::string>> rows;
  sqlite3_exec(db_,
               "SELECT hero_name, COUNT(*) as cnt FROM hands GROUP BY hero_name ORDER BY cnt DESC",
               Callback, &rows, nullptr);
  for (const auto& row : rows) results.push_back({row.at("hero_name"), std::stoll(row.at("cnt"))});
  return results;
}

int64_t HandDatabase::StartSession(const std::string& name) {
  if (!db_) return -1;
  std::ostringstream sql;
  sql << "INSERT INTO sessions (name, start_time) VALUES ('" << name << "', datetime('now'));";
  Execute(sql.str());
  return sqlite3_last_insert_rowid(db_);
}

bool HandDatabase::EndSession(int64_t) { return true; }
bool HandDatabase::AddSessionHand(int64_t, int64_t) { return true; }
std::vector<DBHandInfo> HandDatabase::GetSessionHands(int64_t) const { return {}; }

int HandDatabase::ImportDirectory(const std::string& dir_path) {
  if (!db_) return 0;
  poker_engine::phase4::HandHistoryParser parser;
  auto hands = parser.ParseFromDirectory(dir_path);
  InsertHands(hands);
  return static_cast<int>(hands.size());
}

bool HandDatabase::DeleteHand(int64_t hand_id) {
  if (!db_) return false;
  std::ostringstream sql;
  sql << "DELETE FROM hands WHERE hand_id = " << hand_id << ";";
  Execute("DELETE FROM hands_actions WHERE hand_id = " + std::to_string(hand_id) + ";");
  return Execute(sql.str());
}

bool HandDatabase::DeleteAll() {
  if (!db_) return false;
  Execute("DELETE FROM hands_actions;");
  return Execute("DELETE FROM hands;");
}

#endif  // HAS_SQLITE3

}  // namespace phase6
}  // namespace poker_engine
