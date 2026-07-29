#include "poker_engine/persistence/player_stats_repository.h"

#include <iomanip>
#include <sstream>

#include "poker_engine/base/logging.h"
#include "poker_engine/persistence/database_manager.h"

namespace poker_engine::persistence {

PlayerStatsRepository::PlayerStatsRepository(DatabaseManager& db) : db_(db) {}

void PlayerStatsRepository::RecalculateAll() {
  // 遍历所有有 hand 记录的玩家
  db_.Query(
      "SELECT DISTINCT player_id FROM hand_players", [this](const std::vector<std::string>& row) {
        int64_t pid = std::stoll(row[0]);

        // total_hands
        int hands = 0;
        db_.Query("SELECT COUNT(*) FROM hand_players WHERE player_id = " + std::to_string(pid),
                  [&](const std::vector<std::string>& r) {
                    hands = std::stoi(r[0]);
                    return false;
                  });

        // hands_won
        int won = 0;
        db_.Query("SELECT COUNT(*) FROM hand_players WHERE player_id = " + std::to_string(pid) +
                      " AND is_winner = 1",
                  [&](const std::vector<std::string>& r) {
                    won = std::stoi(r[0]);
                    return false;
                  });

        // total_profit
        double profit = 0;
        db_.Query("SELECT COALESCE(SUM(net_profit), 0) FROM hand_players WHERE player_id = " +
                      std::to_string(pid),
                  [&](const std::vector<std::string>& r) {
                    profit = std::stod(r[0]);
                    return false;
                  });

        // VPIP: 参与底池的手牌比例
        int vpip_hands = 0;
        db_.Query("SELECT COUNT(*) FROM hand_players WHERE player_id = " + std::to_string(pid) +
                      " AND starting_chips > ending_chips + net_profit",
                  [&](const std::vector<std::string>& r) {
                    vpip_hands = std::stoi(r[0]);
                    return false;
                  });

        double vpip = hands > 0 ? (100.0 * vpip_hands / hands) : 0.0;
        double win_rate = hands > 0 ? (100.0 * won / hands) : 0.0;

        // 写入 player_stats
        db_.Execute(
            "INSERT OR REPLACE INTO player_stats "
            "(player_id, total_hands, hands_won, total_profit, vpip_pct, "
            "pfr_pct, agg_factor, win_rate) VALUES (" +
            std::to_string(pid) + ", " + std::to_string(hands) + ", " + std::to_string(won) + ", " +
            std::to_string(profit) + ", " + std::to_string(vpip) + ", 0, 0, " +
            std::to_string(win_rate) + ")");

        return true;
      });

  LOG_INFO("Recalculated all player stats");
}

void PlayerStatsRepository::UpdateFromHand(int64_t player_id, bool won, int64_t profit,
                                           bool acted_preflop, bool raised_preflop) {
  // 增量更新：不重新计算全部，只 +1
  db_.Execute(
      "UPDATE player_stats SET "
      "total_hands = total_hands + 1, "
      "hands_won = hands_won + " +
      std::to_string(won ? 1 : 0) +
      ", "
      "total_profit = total_profit + " +
      std::to_string(static_cast<double>(profit)) +
      ", "
      "win_rate = (hands_won * 100.0) / (total_hands + 1), "
      "vpip_pct = ((vpip_pct / 100.0 * (total_hands - 1)) + 1.0) * 100.0 / (total_hands + 1), "
      "pfr_pct = ((pfr_pct / 100.0 * (total_hands - 1)) + " +
      std::to_string(raised_preflop ? 1.0 : 0.0) +
      ") * 100.0 / (total_hands + 1), "
      "last_updated = datetime('now') "
      "WHERE player_id = " +
      std::to_string(player_id));
}

std::vector<PlayerStatsEntry> PlayerStatsRepository::GetLeaderboard(int limit) const {
  std::vector<PlayerStatsEntry> result;

  std::string sql =
      "SELECT ps.player_id, p.username, p.display_name, "
      "ps.total_hands, ps.hands_won, ps.win_rate, "
      "ps.vpip_pct, ps.pfr_pct, ps.agg_factor, "
      "ps.total_profit, p.elo_rating "
      "FROM player_stats ps "
      "JOIN players p ON ps.player_id = p.id "
      "ORDER BY ps.total_profit DESC LIMIT " +
      std::to_string(limit);

  db_.Query(sql, [&](const std::vector<std::string>& row) {
    PlayerStatsEntry e;
    e.player_id = std::stoll(row[0]);
    e.username = row[1];
    e.display_name = row[2];
    e.hands_played = std::stoll(row[3]);
    e.hands_won = std::stoll(row[4]);
    e.win_rate = std::stod(row[5]);
    e.vpip_pct = std::stod(row[6]);
    e.pfr_pct = std::stod(row[7]);
    e.agg_factor = std::stod(row[8]);
    e.total_profit = static_cast<int64_t>(std::stod(row[9]));
    e.elo_rating = std::stoll(row[10]);
    result.push_back(e);
    return true;
  });

  return result;
}

std::optional<PlayerStatsEntry> PlayerStatsRepository::GetStats(int64_t player_id) const {
  std::optional<PlayerStatsEntry> result;

  db_.Query(
      "SELECT ps.player_id, p.username, p.display_name, "
      "ps.total_hands, ps.hands_won, ps.win_rate, "
      "ps.vpip_pct, ps.pfr_pct, ps.agg_factor, "
      "ps.total_profit, p.elo_rating "
      "FROM player_stats ps JOIN players p ON ps.player_id = p.id "
      "WHERE ps.player_id = " +
          std::to_string(player_id),
      [&](const std::vector<std::string>& row) {
        PlayerStatsEntry e;
        e.player_id = std::stoll(row[0]);
        e.username = row[1];
        e.display_name = row[2];
        e.hands_played = std::stoll(row[3]);
        e.hands_won = std::stoll(row[4]);
        e.win_rate = std::stod(row[5]);
        e.vpip_pct = std::stod(row[6]);
        e.pfr_pct = std::stod(row[7]);
        e.agg_factor = std::stod(row[8]);
        e.total_profit = static_cast<int64_t>(std::stod(row[9]));
        e.elo_rating = std::stoll(row[10]);
        result = e;
        return false;
      });

  return result;
}

}  // namespace poker_engine::persistence
