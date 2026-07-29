#include "poker_engine/phase14/player_repository.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/phase14/database.h"

namespace poker_engine::phase14 {

// ===================== PlayerInfo =====================
std::string PlayerInfo::ToString() const {
  std::ostringstream oss;
  oss << "#" << player_id << " " << (display_name.empty() ? name : display_name)
      << " | Hands: " << hands_played << " | Won: " << hands_won << " | Net: $" << int(total_net);
  return oss.str();
}

// ===================== PlayerStats =====================
std::string PlayerStats::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << player_name << "\n";
  oss << "  VPIP: " << vpip_pct * 100 << "% (" << hands_vpip << "/" << hands_seen << ")\n";
  oss << "  PFR:  " << pfr_pct * 100 << "% (" << hands_pfr << "/" << hands_seen << ")\n";
  oss << "  AF:   " << af << "\n";
  oss << "  WR:   " << win_rate * 100 << "%\n";
  oss << "  BB/100: " << avg_bb_per_100 << "\n";
  return oss.str();
}

// ===================== PlayerRepository =====================

PlayerRepository::PlayerRepository(Database& db) : db_(db) {
  db_.Execute(R"(
        CREATE TABLE IF NOT EXISTS players (
            player_id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            display_name TEXT,
            total_buy_in REAL DEFAULT 0,
            total_cash_out REAL DEFAULT 0,
            hands_played INTEGER DEFAULT 0,
            hands_won INTEGER DEFAULT 0,
            total_net REAL DEFAULT 0,
            created_at TEXT DEFAULT (datetime('now')),
            last_seen TEXT DEFAULT (datetime('now')),
            notes TEXT
        )
    )");
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

int32_t PlayerRepository::CreatePlayer(const std::string& name, const std::string& display_name) {
  std::string err;
  if (!db_.Execute("INSERT INTO players (name, display_name) VALUES ('" + EscapeSQL(name) + "', '" +
                       EscapeSQL(display_name) + "')",
                   err)) {
    std::cerr << "CreatePlayer error: " << err << "\n";
    return -1;
  }
  return static_cast<int32_t>(db_.LastInsertRowId());
}

PlayerInfo PlayerRepository::GetPlayer(int32_t player_id) {
  PlayerInfo pi;
  auto rs = db_.Query(
      "SELECT player_id, name, display_name, total_buy_in, total_cash_out, "
      "hands_played, hands_won, total_net, created_at, last_seen, notes "
      "FROM players WHERE player_id = " +
      std::to_string(player_id));

  if (rs.Next()) {
    Row row = rs.GetRow();
    pi.player_id = row.GetInt(0);
    pi.name = row.GetString(1);
    pi.display_name = row.GetString(2);
    pi.total_buy_in = row.GetDouble(3);
    pi.total_cash_out = row.GetDouble(4);
    pi.hands_played = row.GetInt64(5);
    pi.hands_won = row.GetInt64(6);
    pi.total_net = row.GetDouble(7);
    pi.created_at = row.GetString(8);
    pi.last_seen = row.GetString(9);
    pi.notes = row.GetString(10);
  }
  return pi;
}

std::vector<PlayerInfo> PlayerRepository::FindPlayers(const std::string& name_pattern) {
  std::vector<PlayerInfo> result;
  std::string escaped = EscapeSQL(name_pattern);

  auto rs = db_.Query(
      "SELECT player_id, name, display_name, total_buy_in, total_cash_out, "
      "hands_played, hands_won, total_net, created_at, last_seen, notes "
      "FROM players WHERE name LIKE '%" +
      escaped + "%' OR display_name LIKE '%" + escaped + "%'");

  while (rs.Next()) {
    Row row = rs.GetRow();
    PlayerInfo pi;
    pi.player_id = row.GetInt(0);
    pi.name = row.GetString(1);
    pi.display_name = row.GetString(2);
    pi.total_buy_in = row.GetDouble(3);
    pi.total_cash_out = row.GetDouble(4);
    pi.hands_played = row.GetInt64(5);
    pi.hands_won = row.GetInt64(6);
    pi.total_net = row.GetDouble(7);
    pi.created_at = row.GetString(8);
    pi.last_seen = row.GetString(9);
    pi.notes = row.GetString(10);
    result.push_back(pi);
  }
  return result;
}

void PlayerRepository::UpdatePlayerStats(int32_t player_id, double buy_in, double cash_out) {
  db_.Execute("UPDATE players SET total_buy_in = total_buy_in + " + std::to_string(buy_in) +
              ", total_cash_out = total_cash_out + " + std::to_string(cash_out) +
              ", last_seen = datetime('now') WHERE player_id = " + std::to_string(player_id));
}

std::vector<PlayerInfo> PlayerRepository::GetAllPlayers() {
  std::vector<PlayerInfo> result;
  auto rs = db_.Query(
      "SELECT player_id, name, display_name, total_buy_in, total_cash_out, "
      "hands_played, hands_won, total_net, created_at, last_seen, notes "
      "FROM players ORDER BY player_id");

  while (rs.Next()) {
    Row row = rs.GetRow();
    PlayerInfo pi;
    pi.player_id = row.GetInt(0);
    pi.name = row.GetString(1);
    pi.display_name = row.GetString(2);
    pi.total_buy_in = row.GetDouble(3);
    pi.total_cash_out = row.GetDouble(4);
    pi.hands_played = row.GetInt64(5);
    pi.hands_won = row.GetInt64(6);
    pi.total_net = row.GetDouble(7);
    pi.created_at = row.GetString(8);
    pi.last_seen = row.GetString(9);
    pi.notes = row.GetString(10);
    result.push_back(pi);
  }
  return result;
}

std::vector<PlayerInfo> PlayerRepository::GetActivePlayers(int min_hands) {
  std::vector<PlayerInfo> result;
  auto rs = db_.Query(
      "SELECT player_id, name, display_name, total_buy_in, total_cash_out, "
      "hands_played, hands_won, total_net, created_at, last_seen, notes "
      "FROM players WHERE hands_played >= " +
      std::to_string(min_hands) + " ORDER BY hands_played DESC");

  while (rs.Next()) {
    Row row = rs.GetRow();
    PlayerInfo pi;
    pi.player_id = row.GetInt(0);
    pi.name = row.GetString(1);
    pi.display_name = row.GetString(2);
    pi.total_buy_in = row.GetDouble(3);
    pi.total_cash_out = row.GetDouble(4);
    pi.hands_played = row.GetInt64(5);
    pi.hands_won = row.GetInt64(6);
    pi.total_net = row.GetDouble(7);
    pi.created_at = row.GetString(8);
    pi.last_seen = row.GetString(9);
    pi.notes = row.GetString(10);
    result.push_back(pi);
  }
  return result;
}

PlayerStats PlayerRepository::CalculateStats(int32_t player_id) {
  PlayerStats stats;
  auto info = GetPlayer(player_id);
  stats.player_id = player_id;
  stats.player_name = info.display_name.empty() ? info.name : info.display_name;

  // VPIP
  auto rs1 = db_.Query(
      "SELECT COUNT(DISTINCT hand_id) FROM actions "
      "WHERE player_id = " +
      std::to_string(player_id) + " AND action_type IN ('BET','RAISE','CALL','ALL_IN')");
  if (rs1.Next()) stats.hands_vpip = rs1.GetRow().GetInt(0);

  // Seen hands (simplified: use total played from player info)
  stats.hands_seen =
      static_cast<int>(std::max(static_cast<int64_t>(stats.hands_vpip), info.hands_played));
  if (stats.hands_seen == 0) stats.hands_seen = static_cast<int>(info.hands_played);

  stats.vpip_pct =
      stats.hands_seen > 0 ? static_cast<double>(stats.hands_vpip) / stats.hands_seen : 0;

  // PFR
  auto rs3 = db_.Query(
      "SELECT COUNT(*) FROM actions "
      "WHERE player_id = " +
      std::to_string(player_id) + " AND street = 0 AND action_type IN ('BET','RAISE')");
  if (rs3.Next()) stats.hands_pfr = rs3.GetRow().GetInt(0);
  stats.pfr_pct =
      stats.hands_seen > 0 ? static_cast<double>(stats.hands_pfr) / stats.hands_seen : 0;

  // AF
  auto rs4 = db_.Query(
      "SELECT "
      "SUM(CASE WHEN action_type IN ('BET','RAISE','ALL_IN') THEN 1 ELSE 0 END) as agg, "
      "SUM(CASE WHEN action_type = 'CALL' THEN 1 ELSE 0 END) as calls "
      "FROM actions WHERE player_id = " +
      std::to_string(player_id));
  if (rs4.Next()) {
    Row row = rs4.GetRow();
    stats.bets = std::max(1, row.GetInt(0));
    stats.calls = std::max(1, row.GetInt(1));
  }
  stats.af = static_cast<double>(stats.bets) / stats.calls;

  // Win rate
  stats.hands_won = static_cast<int>(info.hands_won);
  stats.win_rate =
      info.hands_played > 0 ? static_cast<double>(stats.hands_won) / info.hands_played : 0;

  // BB/100
  auto hand_rs = db_.Query(
      "SELECT COUNT(*), SUM(pr.amount_won) FROM player_results pr "
      "WHERE pr.player_id = " +
      std::to_string(player_id));
  if (hand_rs.Next()) {
    Row row = hand_rs.GetRow();
    int hand_count = row.GetInt(0);
    double total_won = row.GetDouble(1);
    stats.avg_bb_per_100 = hand_count > 0 ? (total_won / hand_count) * 100 : 0;
  }

  stats.total_net = info.total_net;
  return stats;
}

std::vector<PlayerStats> PlayerRepository::CalculateAllStats(int min_hands) {
  auto active = GetActivePlayers(min_hands);
  std::vector<PlayerStats> all_stats;
  for (const auto& p : active) {
    all_stats.push_back(CalculateStats(p.player_id));
  }
  return all_stats;
}

std::vector<std::pair<PlayerInfo, PlayerStats>> PlayerRepository::GetLeaderboard(int limit) {
  std::vector<std::pair<PlayerInfo, PlayerStats>> leaderboard;

  auto rs = db_.Query(
      "SELECT player_id, name, display_name, total_buy_in, total_cash_out, "
      "hands_played, hands_won, total_net, created_at, last_seen, notes "
      "FROM players WHERE hands_played >= 1 ORDER BY total_net DESC LIMIT " +
      std::to_string(limit));

  while (rs.Next()) {
    Row row = rs.GetRow();
    PlayerInfo pi;
    pi.player_id = row.GetInt(0);
    pi.name = row.GetString(1);
    pi.display_name = row.GetString(2);
    pi.total_buy_in = row.GetDouble(3);
    pi.total_cash_out = row.GetDouble(4);
    pi.hands_played = row.GetInt64(5);
    pi.hands_won = row.GetInt64(6);
    pi.total_net = row.GetDouble(7);
    pi.created_at = row.GetString(8);
    pi.last_seen = row.GetString(9);
    pi.notes = row.GetString(10);

    leaderboard.push_back({pi, CalculateStats(pi.player_id)});
  }

  return leaderboard;
}

}  // namespace poker_engine::phase14
