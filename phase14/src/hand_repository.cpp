#include "poker_engine/phase14/hand_repository.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/phase14/database.h"

namespace poker_engine::phase14 {

// ===================== HandRecord =====================
std::string HandRecord::ToString() const {
  std::ostringstream oss;
  oss << "Hand #" << hand_id << " @ " << table_name << " (" << num_players << " players, "
      << small_blind << "/" << big_blind;
  if (ante > 0) oss << ", ante " << ante;
  oss << ")\nBoard: ";
  for (const auto& c : community_cards) oss << c << " ";
  oss << "\nDuration: " << duration_ms << "ms";
  return oss.str();
}

// ===================== PlayerHandResult =====================
std::string PlayerHandResult::ToString() const {
  std::ostringstream oss;
  oss << player_name << " (" << hole_cards << "): ";
  if (won)
    oss << "WINS $" << amount_won;
  else
    oss << "lost $" << amount_wagered;
  oss << " | " << best_hand << " | net: $" << net_profit;
  return oss.str();
}

// ===================== HandRepository =====================

HandRepository::HandRepository(Database& db) : db_(db) {
  db_.Execute(R"(
        CREATE TABLE IF NOT EXISTS hands (
            hand_id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id INTEGER DEFAULT 0,
            timestamp TEXT DEFAULT (datetime('now')),
            table_name TEXT,
            num_players INTEGER,
            small_blind REAL,
            big_blind REAL,
            ante REAL,
            community_cards TEXT,
            duration_ms INTEGER
        )
    )");

  db_.Execute(R"(
        CREATE TABLE IF NOT EXISTS player_results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            hand_id INTEGER,
            player_id INTEGER,
            player_name TEXT,
            hole_cards TEXT,
            action_summary TEXT,
            amount_won REAL DEFAULT 0,
            amount_wagered REAL DEFAULT 0,
            net_profit REAL DEFAULT 0,
            won INTEGER DEFAULT 0,
            best_hand TEXT,
            hand_rank INTEGER,
            is_hero INTEGER DEFAULT 0
        )
    )");

  db_.Execute(R"(
        CREATE TABLE IF NOT EXISTS actions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            hand_id INTEGER,
            player_id INTEGER,
            street INTEGER,
            action_type TEXT,
            amount REAL,
            pot_after REAL,
            action_number INTEGER,
            timestamp TEXT DEFAULT (datetime('now'))
        )
    )");

  db_.Execute("CREATE INDEX IF NOT EXISTS idx_hands_timestamp ON hands(timestamp)");
  db_.Execute("CREATE INDEX IF NOT EXISTS idx_player_results_hand ON player_results(hand_id)");
  db_.Execute("CREATE INDEX IF NOT EXISTS idx_player_results_player ON player_results(player_id)");
  db_.Execute("CREATE INDEX IF NOT EXISTS idx_actions_hand ON actions(hand_id)");
  db_.Execute("CREATE INDEX IF NOT EXISTS idx_actions_player ON actions(player_id)");
}

static std::string EscapeSQL(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '\'')
      out += "''";
    else
      out += c;
  }
  return out;
}

int64_t HandRepository::SaveHand(const HandRecord& hand,
                                 const std::vector<PlayerHandResult>& results,
                                 const std::vector<ActionRecord>& actions) {
  ScopedTransaction tx(db_);

  std::string community_str;
  for (size_t i = 0; i < hand.community_cards.size(); i++) {
    if (i > 0) community_str += ",";
    community_str += hand.community_cards[i];
  }

  std::string sql =
      "INSERT INTO hands (session_id, table_name, num_players, small_blind, big_blind, ante, "
      "community_cards, duration_ms, rng_proof) VALUES (" +
      std::to_string(hand.session_id) + ", '" + EscapeSQL(hand.table_name) + "', " +
      std::to_string(hand.num_players) + ", " + std::to_string(hand.small_blind) + ", " +
      std::to_string(hand.big_blind) + ", " + std::to_string(hand.ante) + ", '" +
      EscapeSQL(community_str) + "', " + std::to_string(hand.duration_ms) + ", '" +
      EscapeSQL(hand.rng_proof) + "')";

  std::string err;
  if (!db_.Execute(sql, err)) {
    std::cerr << "SaveHand error: " << err << "\n";
    return -1;
  }

  int64_t hand_id = db_.LastInsertRowId();

  for (const auto& r : results) {
    db_.Execute(
        "INSERT INTO player_results (hand_id, player_id, player_name, hole_cards, "
        "action_summary, amount_won, amount_wagered, net_profit, won, best_hand, hand_rank, "
        "is_hero) VALUES (" +
        std::to_string(hand_id) + ", " + std::to_string(r.player_id) + ", '" +
        EscapeSQL(r.player_name) + "', '" + EscapeSQL(r.hole_cards) + "', '" +
        EscapeSQL(r.action_summary) + "', " + std::to_string(r.amount_won) + ", " +
        std::to_string(r.amount_wagered) + ", " + std::to_string(r.net_profit) + ", " +
        (r.won ? "1" : "0") + ", '" + EscapeSQL(r.best_hand) + "', " + std::to_string(r.hand_rank) +
        ", " + (r.is_hero ? "1" : "0") + ")");
  }

  // Update players table stats
  for (const auto& r : results) {
    if (r.player_id > 0) {
      // Ensure player exists (INSERT OR IGNORE for safety)
      db_.Execute("INSERT OR IGNORE INTO players (player_id, name) VALUES (" +
                  std::to_string(r.player_id) + ", '" + EscapeSQL(r.player_name) + "')");
      db_.Execute(
          "UPDATE players SET hands_played = hands_played + 1, "
          "hands_won = hands_won + " +
          std::string(r.won ? "1" : "0") +
          ", "
          "total_net = total_net + " +
          std::to_string(r.net_profit) +
          ", "
          "last_seen = datetime('now') "
          "WHERE player_id = " +
          std::to_string(r.player_id));
    }
  }

  for (const auto& a : actions) {
    db_.Execute(
        "INSERT INTO actions (hand_id, player_id, street, action_type, amount, pot_after, "
        "action_number) VALUES (" +
        std::to_string(hand_id) + ", " + std::to_string(a.player_id) + ", " +
        std::to_string(a.street) + ", '" + EscapeSQL(a.action_type) + "', " +
        std::to_string(a.amount) + ", " + std::to_string(a.pot_after) + ", " +
        std::to_string(a.action_number) + ")");
  }

  tx.Commit();
  return hand_id;
}

std::pair<HandRecord, std::vector<PlayerHandResult>> HandRepository::GetHand(int64_t hand_id) {
  HandRecord hand;
  std::vector<PlayerHandResult> results;

  auto rs = db_.Query(
      "SELECT hand_id, session_id, timestamp, table_name, num_players, "
      "small_blind, big_blind, ante, community_cards, duration_ms "
      "FROM hands WHERE hand_id = " +
      std::to_string(hand_id));

  if (rs.Next()) {
    Row row = rs.GetRow();
    hand.hand_id = row.GetInt64(0);
    hand.session_id = row.GetInt64(1);
    hand.timestamp = row.GetString(2);
    hand.table_name = row.GetString(3);
    hand.num_players = row.GetInt(4);
    hand.small_blind = row.GetDouble(5);
    hand.big_blind = row.GetDouble(6);
    hand.ante = row.GetDouble(7);

    std::string comm_str = row.GetString(8);
    // Parse comma-separated community cards
    if (!comm_str.empty()) {
      std::stringstream ss(comm_str);
      std::string token;
      while (std::getline(ss, token, ',')) {
        hand.community_cards.push_back(token);
      }
    }

    hand.duration_ms = row.GetInt64(9);
  }

  auto pr = db_.Query(
      "SELECT id, hand_id, player_id, player_name, hole_cards, "
      "action_summary, amount_won, amount_wagered, net_profit, won, best_hand, hand_rank, is_hero "
      "FROM player_results WHERE hand_id = " +
      std::to_string(hand_id));

  while (pr.Next()) {
    Row r = pr.GetRow();
    PlayerHandResult phr;
    phr.hand_id = r.GetInt64(1);
    phr.player_id = r.GetInt(2);
    phr.player_name = r.GetString(3);
    phr.hole_cards = r.GetString(4);
    phr.action_summary = r.GetString(5);
    phr.amount_won = r.GetDouble(6);
    phr.amount_wagered = r.GetDouble(7);
    phr.net_profit = r.GetDouble(8);
    phr.won = r.GetInt(9) != 0;
    phr.best_hand = r.GetString(10);
    phr.hand_rank = r.GetInt(11);
    phr.is_hero = r.GetInt(12) != 0;
    results.push_back(phr);
  }

  return {hand, results};
}

std::vector<ActionRecord> HandRepository::GetHandActions(int64_t hand_id) {
  std::vector<ActionRecord> actions;

  auto rs = db_.Query(
      "SELECT id, hand_id, player_id, street, action_type, amount, pot_after, action_number, "
      "timestamp "
      "FROM actions WHERE hand_id = " +
      std::to_string(hand_id) + " ORDER BY action_number");

  while (rs.Next()) {
    Row row = rs.GetRow();
    ActionRecord ar;
    ar.id = row.GetInt64(0);
    ar.hand_id = row.GetInt64(1);
    ar.player_id = row.GetInt(2);
    ar.street = row.GetInt(3);
    ar.action_type = row.GetString(4);
    ar.amount = row.GetDouble(5);
    ar.pot_after = row.GetDouble(6);
    ar.action_number = row.GetInt(7);
    ar.timestamp = row.GetString(8);
    actions.push_back(ar);
  }

  return actions;
}

std::vector<HandRecord> HandRepository::ListHands(int limit, int offset) {
  std::vector<HandRecord> hands;

  auto rs = db_.Query(
      "SELECT hand_id, session_id, timestamp, table_name, num_players, "
      "small_blind, big_blind, ante, community_cards, duration_ms "
      "FROM hands ORDER BY hand_id DESC LIMIT " +
      std::to_string(limit) + " OFFSET " + std::to_string(offset));

  while (rs.Next()) {
    Row row = rs.GetRow();
    HandRecord h;
    h.hand_id = row.GetInt64(0);
    h.session_id = row.GetInt64(1);
    h.timestamp = row.GetString(2);
    h.table_name = row.GetString(3);
    h.num_players = row.GetInt(4);
    h.small_blind = row.GetDouble(5);
    h.big_blind = row.GetDouble(6);
    h.ante = row.GetDouble(7);

    std::string comm = row.GetString(8);
    if (!comm.empty()) {
      std::stringstream ss(comm);
      std::string token;
      while (std::getline(ss, token, ',')) {
        h.community_cards.push_back(token);
      }
    }

    h.duration_ms = row.GetInt64(9);
    hands.push_back(h);
  }

  return hands;
}

std::vector<PlayerHandResult> HandRepository::GetPlayerHands(int32_t player_id, int limit) {
  std::vector<PlayerHandResult> results;

  auto rs = db_.Query(
      "SELECT id, hand_id, player_id, player_name, hole_cards, "
      "action_summary, amount_won, amount_wagered, net_profit, won, best_hand, hand_rank, is_hero "
      "FROM player_results WHERE player_id = " +
      std::to_string(player_id) + " ORDER BY hand_id DESC LIMIT " + std::to_string(limit));

  while (rs.Next()) {
    Row row = rs.GetRow();
    PlayerHandResult r;
    r.hand_id = row.GetInt64(1);
    r.player_id = row.GetInt(2);
    r.player_name = row.GetString(3);
    r.hole_cards = row.GetString(4);
    r.action_summary = row.GetString(5);
    r.amount_won = row.GetDouble(6);
    r.amount_wagered = row.GetDouble(7);
    r.net_profit = row.GetDouble(8);
    r.won = row.GetInt(9) != 0;
    r.best_hand = row.GetString(10);
    r.hand_rank = row.GetInt(11);
    r.is_hero = row.GetInt(12) != 0;
    results.push_back(r);
  }

  return results;
}

int HandRepository::GetHandCount() {
  auto rs = db_.Query("SELECT COUNT(*) FROM hands");
  if (rs.Next()) return rs.GetRow().GetInt(0);
  return 0;
}

int64_t HandRepository::GetLastHandId() { return db_.LastInsertRowId(); }

}  // namespace poker_engine::phase14
