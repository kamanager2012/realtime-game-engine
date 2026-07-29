#include <iomanip>
#include <iostream>

#include "poker_engine/phase14/database_manager.h"
#include "poker_engine/phase14/hand_repository.h"
#include "poker_engine/phase14/player_repository.h"
#include "poker_engine/phase14/stat_repository.h"

using namespace poker_engine::phase14;

void PrintHelp() {
  std::cout << "Poker DB Tool v1.4\n\n"
            << "Usage: db_tool <command> [args...]\n\n"
            << "Commands:\n"
            << "  init <path>         Initialize new database\n"
            << "  health              Check database health\n"
            << "  stats               Database overview stats\n"
            << "  players [pattern]   List players\n"
            << "  player <id>         Detailed player report\n"
            << "  leaderboard [n]     Show top N players\n"
            << "  hands [n]           List recent hands\n"
            << "  hand <id>           Show hand details\n"
            << "  variance <id>       Variance analysis\n"
            << "  opponents <id>      Opponent breakdown\n"
            << "  backup <dest>       Backup database\n"
            << "  query <sql>         Execute raw SQL\n";
}

bool EnsureDB(DatabaseManager& mgr, const std::string& path = "poker_engine.db") {
  if (!mgr.IsHealthy()) {
    return mgr.Initialize(path);
  }
  return true;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintHelp();
    return 1;
  }

  std::string cmd = argv[1];

  if (cmd == "init" && argc >= 3) {
    auto& mgr = DatabaseManager::Instance();
    if (mgr.Initialize(argv[2])) {
      std::cout << "Database initialized at: " << argv[2] << "\n";
      std::cout << mgr.GetDatabaseStats() << "\n";
    }
    mgr.Shutdown();
  } else if (cmd == "health") {
    auto& mgr = DatabaseManager::Instance();
    if (EnsureDB(mgr)) {
      if (mgr.IsHealthy()) {
        std::cout << "Database is healthy\n";
        std::cout << "  SQLite: " << mgr.GetDatabase().GetVersion() << "\n";
        std::cout << "  Path: " << mgr.GetDatabase().GetFilePath() << "\n";
      } else {
        std::cout << "Database health check failed\n";
      }
    }
    mgr.Shutdown();
  } else if (cmd == "stats") {
    auto& mgr = DatabaseManager::Instance();
    if (EnsureDB(mgr)) std::cout << mgr.GetDatabaseStats();
    mgr.Shutdown();
  } else if (cmd == "players") {
    auto& mgr = DatabaseManager::Instance();
    if (!EnsureDB(mgr)) return 1;
    std::string pattern = (argc >= 3) ? argv[2] : "";
    auto& repo = mgr.GetPlayerRepo();
    auto players = pattern.empty() ? repo.GetAllPlayers() : repo.FindPlayers(pattern);
    std::cout << "Players (" << players.size() << " found):\n";
    for (const auto& p : players) {
      std::cout << "  " << p.ToString() << "\n";
    }
    mgr.Shutdown();
  } else if (cmd == "player" && argc >= 3) {
    auto& mgr = DatabaseManager::Instance();
    if (!EnsureDB(mgr)) return 1;
    std::cout << mgr.GetPlayerReport(std::stoi(argv[2]));
    mgr.Shutdown();
  } else if (cmd == "leaderboard") {
    auto& mgr = DatabaseManager::Instance();
    if (!EnsureDB(mgr)) return 1;
    int limit = (argc >= 3) ? std::stoi(argv[2]) : 20;
    auto lb = mgr.GetPlayerRepo().GetLeaderboard(limit);
    std::cout << "=== Leaderboard (Top " << limit << ") ===\n\n";
    int rank = 1;
    for (const auto& [info, stats] : lb) {
      std::string name = info.display_name.empty() ? info.name : info.display_name;
      std::cout << "#" << rank++ << " " << name << " | Hands: " << info.hands_played << " | Net: $"
                << int(info.total_net) << "\n";
    }
    mgr.Shutdown();
  } else if (cmd == "hands") {
    auto& mgr = DatabaseManager::Instance();
    if (!EnsureDB(mgr)) return 1;
    int limit = (argc >= 3) ? std::stoi(argv[2]) : 20;
    auto hands = mgr.GetHandRepo().ListHands(limit, 0);
    for (const auto& h : hands) {
      std::cout << "#" << h.hand_id << " | " << h.timestamp << " | " << h.table_name << " | "
                << h.num_players << " players\n";
    }
    mgr.Shutdown();
  } else if (cmd == "hand" && argc >= 3) {
    auto& mgr = DatabaseManager::Instance();
    if (!EnsureDB(mgr)) return 1;
    auto hand_data = mgr.GetHandRepo().GetHand(std::stoll(argv[2]));
    std::cout << hand_data.first.ToString() << "\n\n";
    for (const auto& r : hand_data.second) {
      std::cout << "  " << r.ToString() << "\n";
    }
    mgr.Shutdown();
  } else if (cmd == "variance" && argc >= 3) {
    auto& mgr = DatabaseManager::Instance();
    if (!EnsureDB(mgr)) return 1;
    auto st = mgr.GetStatRepo().GetVarianceStats(std::stoi(argv[2]));
    std::cout << "=== Variance Analysis ===\n";
    std::cout << "  Sample: " << st.sample_size << " hands\n";
    std::cout << "  Mean BB/100: " << st.mean_bb100 << "\n";
    std::cout << "  Std Dev: " << st.std_dev_bb100 << "\n";
    std::cout << "  95% CI: [" << st.ci95_low << ", " << st.ci95_high << "]\n";
    std::cout << "  Max Drawdown: $" << int(st.max_drawdown) << "\n";
    mgr.Shutdown();
  } else if (cmd == "opponents" && argc >= 3) {
    auto& mgr = DatabaseManager::Instance();
    if (!EnsureDB(mgr)) return 1;
    auto opps = mgr.GetStatRepo().GetOpponentStats(std::stoi(argv[2]));
    for (const auto& opp : opps) {
      std::cout << "vs " << opp.opponent_name << " | " << opp.hands_played << " hands"
                << " | WR " << int(opp.win_rate_vs * 100) << "%"
                << " | Net $" << int(opp.net_vs) << "\n";
    }
    mgr.Shutdown();
  } else if (cmd == "backup" && argc >= 3) {
    auto& mgr = DatabaseManager::Instance();
    if (!EnsureDB(mgr)) return 1;
    if (mgr.Backup(argv[2])) {
      std::cout << "Backup successful: " << argv[2] << "\n";
    } else {
      std::cout << "Backup failed\n";
    }
    mgr.Shutdown();
  } else if (cmd == "query" && argc >= 3) {
    auto& mgr = DatabaseManager::Instance();
    if (!EnsureDB(mgr)) return 1;
    std::string sql;
    for (int i = 2; i < argc; i++) {
      if (i > 2) sql += " ";
      sql += argv[i];
    }
    auto rs = mgr.GetDatabase().Query(sql);
    int cols = rs.ColumnCount();
    while (rs.Next()) {
      Row row = rs.GetRow();
      for (int i = 0; i < cols; i++) {
        std::cout << (row.IsNull(i) ? "NULL" : row.GetString(i)) << "\t";
      }
      std::cout << "\n";
    }
    mgr.Shutdown();
  } else if (cmd == "ledger-check") {
    auto& mgr = DatabaseManager::Instance();
    if (!EnsureDB(mgr)) return 1;
    auto report = mgr.GetChipLedger().Reconcile();
    std::cout << report.ToString() << "\n";
    std::cout << (report.all_balanced ? "RESULT: LEDGER BALANCED"
                                              : "RESULT: LEDGER MISMATCH")
              << "\n";
    mgr.Shutdown();
    return report.all_balanced ? 0 : 2;
  } else if (cmd == "ledger-export" && argc >= 3) {
    auto& mgr = DatabaseManager::Instance();
    if (!EnsureDB(mgr)) return 1;
    if (mgr.GetChipLedger().ExportLedgerCSV(argv[2])) {
      std::cout << "Exported ledger to " << argv[2] << "\n";
    } else {
      std::cout << "Export failed (check path/permissions)\n";
    }
    mgr.Shutdown();
  } else {
    PrintHelp();
    return 1;
  }

  return 0;
}
