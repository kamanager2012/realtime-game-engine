#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "poker_engine/evaluator/card.h"
#include "poker_engine/phase6/api_server.h"
#include "poker_engine/phase6/hand_database.h"
#include "poker_engine/phase6/icfr_solver.h"
#include "poker_engine/phase6/range_tracker.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase6;
using namespace poker_engine::range;

void PrintUsage(const char* prog) {
  std::cout << "Poker Phase 6 Tool v0.6\n"
            << "Usage: " << prog << " <command> [args...]\n\n"
            << "Commands:\n"
            << "  icfr-solve <hero> <villain> [flop]    Solve ICFR best response\n"
            << "  icfr-learn <handfile> <villain_name>  Learn blueprint from HH\n"
            << "  track <hero_range> <action> ...       Track range in real-time\n"
            << "  db-create <path>                      Create SQLite database\n"
            << "  db-import <path> <hh_dir>             Import HH files\n"
            << "  db-query <path> <player>              Query hands by player\n"
            << "  db-stats <path>                       Database statistics\n"
            << "  api-server [port]                     Start REST API server\n"
            << "\nExamples:\n"
            << "  " << prog << " icfr-solve 'AKs' '22+' 'QhJd7c'\n"
            << "  " << prog << " track '22+,A2s+,K2s+' 'raise' 0.75\n"
            << "  " << prog << " db-create ./poker.db\n";
}

void CmdICFRSolve(const std::string& hero_str, const std::string& villain_str,
                  const std::string& flop_str) {
  ICFRConfig config;
  config.iterations = 1000;
  config.mc_samples = 2000;
  config.verbose = true;
  config.blueprint_weight = 0.5;

  ICFRSolver solver(config);
  solver.SetHeroRange(Range::FromString(hero_str));
  solver.SetVillainRange(Range::FromString(villain_str));

  if (!flop_str.empty()) {
    std::vector<Card> board;
    for (size_t i = 0; i + 1 < flop_str.size(); i += 2) {
      board.push_back(Card::Parse(flop_str.substr(i, 2)));
    }
    solver.SetBoard(board);
  }

  auto result = solver.Solve();
  std::cout << result.ToString() << "\n";
}

void CmdICFRLearn(const std::string& hand_file, const std::string& villain_name) {
  ICFRConfig config;
  config.iterations = 500;
  config.mc_samples = 1000;
  config.verbose = true;

  ICFRSolver solver(config);
  solver.SetHeroRange(Range::FullCombinatorial());
  solver.SetVillainRange(Range::FullCombinatorial());

  poker_engine::phase4::HandHistoryParser parser;
  auto hands = parser.ParseFromDirectory(hand_file);
  std::cout << "Parsed " << hands.size() << " hands from " << hand_file << "\n";

  solver.LearnBlueprintFromHistory(hands, villain_name);
  std::cout << "Blueprint learned! Use icfr-solve to apply it.\n";
}

void CmdTrack(const std::string& prior_range, int argc, char* argv[]) {
  RangeTracker tracker;
  tracker.SetPriorRange(prior_range);

  std::cout << "=== Range Tracker ===\n";
  std::cout << "Prior: " << prior_range << " (" << tracker.GetCurrentRange().remaining_combos
            << " combos)\n\n";

  for (int i = 0; i + 1 < argc; i += 2) {
    std::string action_str = argv[i + 1];
    double amount = (i + 2 < argc) ? std::stod(argv[i + 2]) : 0.0;

    TrackingObservation obs;
    obs.street = 0;
    obs.pot = 100.0;
    obs.amount = amount;

    if (action_str == "fold")
      obs.action = ActionObserved::FOLD;
    else if (action_str == "check")
      obs.action = ActionObserved::CHECK;
    else if (action_str == "call")
      obs.action = ActionObserved::CALL;
    else if (action_str == "raise" || action_str == "bet")
      obs.action = ActionObserved::RAISE;
    else if (action_str == "all-in")
      obs.action = ActionObserved::ALL_IN;
    else
      obs.action = ActionObserved::CHECK;

    tracker.ObserveAction(obs);
    std::cout << "After " << ActionObservedName[static_cast<int>(obs.action)] << " $" << amount
              << ":\n";
    auto result = tracker.GetCurrentRange();
    std::cout << result.ToString() << "\n";
  }
}

void CmdDBCreate(const std::string& db_path) {
  auto db = std::make_shared<HandDatabase>();
  if (!db->Open(db_path)) {
    std::cerr << "Failed to open: " << db->GetLastError() << "\n";
    return;
  }
  if (!db->CreateSchema()) {
    std::cerr << "Failed to create schema: " << db->GetLastError() << "\n";
    return;
  }
  std::cout << "Database created at: " << db_path << "\n";
  std::cout << "Total hands: " << db->TotalHands() << "\n";
}

void CmdDBImport(const std::string& db_path, const std::string& hh_dir) {
  auto db = std::make_shared<HandDatabase>();
  if (!db->Open(db_path)) {
    std::cerr << "Failed to open: " << db->GetLastError() << "\n";
    return;
  }
  db->CreateSchema();

  int imported = db->ImportDirectory(hh_dir);
  std::cout << "Imported " << imported << " hands from " << hh_dir << "\n";
  std::cout << "Total in DB: " << db->TotalHands() << "\n";
}

void CmdDBQuery(const std::string& db_path, const std::string& player) {
  auto db = std::make_shared<HandDatabase>();
  if (!db->Open(db_path)) {
    std::cerr << "Failed to open: " << db->GetLastError() << "\n";
    return;
  }

  DBQueryOptions opts;
  opts.player_filter = player;
  opts.limit = 10;

  auto hands = db->QueryHands(opts);
  std::cout << "Found " << hands.size() << " hands for " << player << ":\n\n";

  for (const auto& h : hands) {
    std::cout << "  #" << h.hand_id << " | " << h.site << " | " << h.hero_cards
              << " | Board: " << h.board << " | Pot: $" << h.total_pot << " | " << h.result << "\n";
  }
}

void CmdDBStats(const std::string& db_path) {
  auto db = std::make_shared<HandDatabase>();
  if (!db->Open(db_path)) {
    std::cerr << "Failed to open: " << db->GetLastError() << "\n";
    return;
  }

  std::cout << "Total hands: " << db->TotalHands() << "\n\n";

  auto sites = db->GetSites();
  std::cout << "Sites:\n";
  for (const auto& [site, count] : sites) std::cout << "  " << site << ": " << count << " hands\n";

  DBQueryOptions opts;
  auto agg = db->GetAggregates(opts);
  std::cout << "\nAggregates:\n";
  std::cout << "  Total net: $" << agg.total_net << "\n";
  std::cout << "  Avg pot: $" << agg.avg_pot << "\n";
  std::cout << "  BB/100: " << agg.avg_bb100 << "\n";
  std::cout << "  Win rate: " << agg.win_rate << "%\n";
}

void CmdAPIServer(int port) {
  APIConfig config;
  config.port = port;
  config.thread_count = 2;

  APIServer server(config);

  auto db = std::make_shared<HandDatabase>();
  db->Open("./poker.db");
  db->CreateSchema();
  server.SetDatabase(db);

  auto solver = std::make_shared<ICFRSolver>(ICFRConfig{500, 1000, 1.0, 0.5, false});
  server.SetSolver(solver);

  std::cout << "Starting API server on port " << port << "...\n";
  std::cout << "Endpoints:\n";
  std::cout << "  GET  /health\n";
  std::cout << "  POST /parse     - Parse hand history text\n";
  std::cout << "  POST /equity    - hero=&villain=&flop=\n";
  std::cout << "  POST /icm       - payouts=&chips=\n";
  std::cout << "  POST /solve     - hero=&villain=&flop=\n";
  std::cout << "  GET  /stats\n";

  if (!server.Start()) {
    std::cerr << "Failed to start server\n";
  }
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string cmd = argv[1];

  if (cmd == "icfr-solve" && argc >= 4) {
    std::string flop = (argc >= 5) ? argv[4] : "";
    CmdICFRSolve(argv[2], argv[3], flop);
  } else if (cmd == "icfr-learn" && argc >= 4) {
    CmdICFRLearn(argv[2], argv[3]);
  } else if (cmd == "track" && argc >= 3) {
    CmdTrack(argv[2], argc - 2, argv + 2);
  } else if (cmd == "db-create" && argc >= 3) {
    CmdDBCreate(argv[2]);
  } else if (cmd == "db-import" && argc >= 4) {
    CmdDBImport(argv[2], argv[3]);
  } else if (cmd == "db-query" && argc >= 4) {
    CmdDBQuery(argv[2], argv[3]);
  } else if (cmd == "db-stats" && argc >= 3) {
    CmdDBStats(argv[2]);
  } else if (cmd == "api-server") {
    int port = (argc >= 3) ? std::stoi(argv[2]) : 8080;
    CmdAPIServer(port);
  } else {
    PrintUsage(argv[0]);
    return 1;
  }

  return 0;
}
