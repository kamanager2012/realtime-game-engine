#include <iostream>
#include <string>

#include "poker_engine/phase3/batch_simulator.h"
#include "poker_engine/phase3/flop_explorer.h"
#include "poker_engine/phase3/hand_replayer.h"
#include "poker_engine/phase3/spot_solver.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase3;
using namespace poker_engine::range;

void PrintUsage(const char* prog) {
  std::cout << "Poker Phase 3 Tool v0.3\n"
            << "Usage: " << prog << " <command> [args...]\n\n"
            << "Commands:\n"
            << "  flop <cards>              Analyze flop (e.g. 'ThJh7s')\n"
            << "  flop-categories           Analyze all flop categories\n"
            << "  spot <hero> <villain> <board> <pot> <call>\n"
            << "                            Solve a spot\n"
            << "  batch <hero> <villain> <n> Run batch simulation\n"
            << "  batch-run                 Run preset batch scenarios\n";
}

void CmdFlop(const std::string& flop_cards) {
  Range default_hero = Range::FromString("AKs,QQ+,AKo");
  Range default_villain = Range::FromString("22+,A2s+,K2s+,Q2s+,J2s+");

  FlopExplorer explorer;
  explorer.SetHeroRange(default_hero);
  explorer.SetVillainRange(default_villain);

  auto analysis = explorer.AnalyzeFlop(flop_cards, 20000);
  std::cout << "\n" << analysis.ToString() << "\n\n";

  auto runouts = explorer.AnalyzeTurnRunouts(flop_cards, 15000);
  std::cout << "Turn runouts analyzed: " << runouts.total_runouts << "\n";
  std::cout << "Average hero equity on turn: " << int(runouts.avg_equity * 100) << "%\n";
  std::cout << "Equity std deviation: " << int(runouts.equity_std * 100) << "%\n";
}

void CmdFlopCategories() {
  Range hero = Range::FromString("AKs,QQ+,AKo");
  Range villain = Range::FromString("22+,A2s+,K2s+,Q2s+");

  FlopExplorer explorer;
  explorer.SetHeroRange(hero);
  explorer.SetVillainRange(villain);
  explorer.AnalyzeFlopCategories(10000);
}

void CmdSpot(const std::string& hero_str, const std::string& villain_str, const std::string& board,
             double pot, double to_call) {
  auto result = SpotSolver::QuickSolve(hero_str, villain_str, board, pot, to_call);
  std::cout << result.ToString() << "\n";
}

void CmdBatch(const std::string& hero_range, const std::string& villain_range, int n_hands) {
  BatchConfig config;
  config.iterations = 5000;
  config.initial_stack = 100.0;
  config.big_blind = 1.0;
  config.verbose = true;

  BatchSimulator sim(config);
  sim.AddPlayer("Hero", hero_range);
  sim.AddPlayer("Villain", villain_range);

  auto results = sim.Run(n_hands);
  std::cout << "\n" << sim.StatsReport() << "\n";
}

void CmdBatchRun() {
  BatchConfig config;
  config.iterations = 10000;
  config.initial_stack = 100.0;
  config.big_blind = 1.0;
  config.verbose = true;

  struct Scenario {
    std::string name;
    std::string hero;
    std::string villain;
  };

  std::vector<Scenario> scenarios = {
      {"AA vs random", "AA", "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,A2o+,K2o+"},
      {"KK vs random", "KK", "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,A2o+,K2o+"},
      {"AKs vs TT+", "AKs", "TT+,AQo+"},
      {"AKo vs JJ+", "AKo", "JJ+,AKs"},
      {"T9s vs 77+", "T9s", "77+,A2s+,K9s+"},
  };

  for (const auto& sc : scenarios) {
    std::cout << "\n>>> Running: " << sc.name << "\n";
    BatchSimulator sim(config);
    sim.AddPlayer("Hero", sc.hero);
    sim.AddPlayer("Villain", sc.villain);
    sim.Run(500);
    std::cout << sim.StatsReport();
  }
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string cmd = argv[1];

  if (cmd == "flop" && argc >= 3) {
    CmdFlop(argv[2]);
  } else if (cmd == "flop-categories") {
    CmdFlopCategories();
  } else if (cmd == "spot" && argc >= 7) {
    CmdSpot(argv[2], argv[3], argv[4], std::stod(argv[5]), std::stod(argv[6]));
  } else if (cmd == "batch" && argc >= 4) {
    CmdBatch(argv[2], argv[3], std::stoi(argv[4]));
  } else if (cmd == "batch-run") {
    CmdBatchRun();
  } else {
    PrintUsage(argv[0]);
    return 1;
  }

  return 0;
}
