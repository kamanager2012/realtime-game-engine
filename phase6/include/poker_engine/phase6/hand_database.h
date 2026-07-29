#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "poker_engine/phase4/hh_parser.h"

// Conditional SQLite3 support
#ifdef HAS_SQLITE3
#include <sqlite3.h>
#endif

namespace poker_engine {
namespace phase6 {

struct DBHandInfo {
  int64_t hand_id = 0;
  std::string site;
  std::string game_type;
  std::string hero_name;
  std::string hero_cards;
  std::string board;
  double big_blind = 0;
  double total_pot = 0;
  double hero_net = 0;
  std::string result;
  std::string timestamp;
};

struct DBQueryOptions {
  std::string player_filter;
  std::string site_filter;
  std::string game_type_filter;
  double min_bb = -1;
  double max_bb = -1;
  std::string date_from;
  std::string date_to;
  int limit = 1000;
  int offset = 0;
};

struct AggResult {
  int64_t hand_count = 0;
  double avg_pot = 0;
  double avg_bb100 = 0;
  double total_net = 0;
  double win_rate = 0;
};

#ifdef HAS_SQLITE3

class HandDatabase {
 public:
  HandDatabase();
  ~HandDatabase();

  // Open/close connection
  bool Open(const std::string& db_path);
  void Close();
  bool IsOpen() const;

  // Schema management
  bool CreateSchema();
  bool Migrate(int target_version = 3);

  // CRUD operations
  bool InsertHand(const poker_engine::phase4::HandHistory& hh);
  bool InsertHands(const std::vector<poker_engine::phase4::HandHistory>& hands);

  // Queries
  std::vector<DBHandInfo> QueryHands(const DBQueryOptions& opts) const;
  std::vector<DBHandInfo> GetHandHistory(int64_t hand_id) const;
  AggResult GetAggregates(const DBQueryOptions& opts) const;
  std::vector<std::pair<std::string, int64_t>> GetSites() const;
  std::vector<std::pair<std::string, int64_t>> GetPlayers() const;
  int64_t TotalHands() const;

  // Session tracking
  int64_t StartSession(const std::string& name);
  bool EndSession(int64_t session_id);
  bool AddSessionHand(int64_t session_id, int64_t hand_id);
  std::vector<DBHandInfo> GetSessionHands(int64_t session_id) const;

  // Batch import
  int ImportDirectory(const std::string& dir_path);

  // Delete
  bool DeleteHand(int64_t hand_id);
  bool DeleteAll();

  std::string GetLastError() const { return last_error_; }

 private:
  sqlite3* db_ = nullptr;
  std::string db_path_;
  mutable std::string last_error_;

  bool Execute(const std::string& sql, std::string* error = nullptr);
  static int Callback(void* data, int cols, char** values, char** names);
};

#else  // No SQLite

class HandDatabase {
 public:
  HandDatabase() {}
  ~HandDatabase() {}
  bool Open(const std::string&) { return false; }
  void Close() {}
  bool IsOpen() const { return false; }
  bool CreateSchema() { return false; }
  bool InsertHand(const poker_engine::phase4::HandHistory&) { return false; }
  bool InsertHands(const std::vector<poker_engine::phase4::HandHistory>&) { return false; }
  std::vector<DBHandInfo> QueryHands(const DBQueryOptions&) const { return {}; }
  std::vector<DBHandInfo> GetHandHistory(int64_t) const { return {}; }
  AggResult GetAggregates(const DBQueryOptions&) const { return {}; }
  std::vector<std::pair<std::string, int64_t>> GetSites() const { return {}; }
  std::vector<std::pair<std::string, int64_t>> GetPlayers() const { return {}; }
  int64_t TotalHands() const { return 0; }
  int64_t StartSession(const std::string&) { return -1; }
  bool EndSession(int64_t) { return false; }
  bool AddSessionHand(int64_t, int64_t) { return false; }
  std::vector<DBHandInfo> GetSessionHands(int64_t) const { return {}; }
  int ImportDirectory(const std::string&) { return 0; }
  bool DeleteHand(int64_t) { return false; }
  bool DeleteAll() { return false; }
  std::string GetLastError() const { return "SQLite3 not available"; }
};

#endif  // HAS_SQLITE3

}  // namespace phase6
}  // namespace poker_engine
