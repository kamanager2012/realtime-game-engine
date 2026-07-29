#include <iostream>
#include <string>

#include "poker_engine/phase8/dashboard_gen.h"
#include "poker_engine/phase8/exploit_engine.h"
#include "poker_engine/phase8/mc_cfr_deep.h"
#include "poker_engine/phase8/multi_agent_sim.h"
#include "poker_engine/phase8/preflop_solver.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase8;
using namespace poker_engine::range;

void PrintUsage(const char* prog) {
  std::cout << "Poker Phase 8 Tool v0.8\n"
            << "Usage: " << prog << " <command> [args...]\n\n"
            << "Commands:\n"
            << "  mc-cfr <hero> <villain> [flop]     MC-CFR Deep solve\n"
            << "  preflop-solve [position]           Solve preflop ranges\n"
            << "  h2h <strategy_a> <strategy_b>      Head to head equity\n"
            << "  agent-sim                          Multi-agent round-robin\n"
            << "  dashboard <output_path>            Generate HTML dashboard\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }
  std::string cmd = argv[1];

  if (cmd == "mc-cfr" && argc >= 4) {
    MCConfig config;
    config.iterations = 2000;
    config.mc_samples_per_iter = 50;
    config.mode = MCRMode::EXTERNAL_SAMPLING;
    config.max_depth = 8;
    config.verbose = true;

    MCCRDeepSolver solver(config);
    solver.SetHeroRange(Range::FromString(argv[2]));
    solver.SetVillainRange(Range::FromString(argv[3]));
    solver.SetPot(20, 7);

    if (argc >= 5) {
      std::string flop = argv[4];
      std::vector<poker_engine::Card> board;
      for (size_t i = 0; i + 1 < flop.size(); i += 2)
        board.push_back(poker_engine::Card::Parse(flop.substr(i, 2)));
      solver.SetBoard(board);
    }

    auto result = solver.Solve();
    std::cout << result.ToString() << "\n";
  } else if (cmd == "preflop-solve") {
    std::string pos = (argc >= 3) ? argv[2] : "BTN";
    PreflopConfig cfg;
    cfg.iterations = 200;
    cfg.mc_samples = 1000;
    cfg.verbose = false;

    PreflopPosition p = BTN;
    std::string upper;
    for (char c : pos) upper += static_cast<char>(toupper(c));
    if (upper == "UTG")
      p = UTG;
    else if (upper == "SB")
      p = SB;
    else if (upper == "BB")
      p = BB;
    else if (upper == "HJ")
      p = HJ;
    else if (upper == "CO")
      p = CO;
    else
      p = BTN;

    cfg.positions = {p};
    PreflopSolver solver(cfg);
    auto all = solver.SolveAll();

    for (const auto& [position, advice_list] : all) {
      std::cout << "\n=== " << PositionName[position] << " Preflop Strategy ===\n";
      int shown = 0;
      for (const auto& a : advice_list) {
        if (a.raise_freq + a.call_freq > 0.1) {
          std::cout << a.ToString() << "\n";
          shown++;
          if (shown >= 15 && a.raise_freq + a.call_freq < 0.4) break;
        }
      }
    }
  } else if (cmd == "h2h" && argc >= 4) {
    double eq = MultiAgentSimulator::HeadToHeadEquity(argv[2], argv[3], 20000);
    std::cout << std::fixed;
    std::cout << "\n=== Head-to-Head Equity ===\n";
    std::cout << "A: " << argv[2] << " → " << int(eq * 100) << "%\n";
    std::cout << "B: " << argv[3] << " → " << int((1.0 - eq) * 100) << "%\n";
  } else if (cmd == "agent-sim") {
    MultiAgentSimulator sim(500);
    sim.AddAgent({"NIT", "TT+,AKs+", 10000, AgentConfig::NIT, 3});
    sim.AddAgent({"TAG", "22+,A2s+,K2s+", 10000, AgentConfig::TAG, 5});
    sim.AddAgent({"LAG", "22+,A2s+,K2s+,Q2s+,J2s+", 10000, AgentConfig::LAG, 7});

    auto outcome = sim.RunRoundRobin();
    std::cout << outcome.ToString() << "\n";
  } else if (cmd == "dashboard" && argc >= 3) {
    DashboardGenerator gen;
    gen.SetTitle("Poker Session Dashboard");

    std::vector<std::pair<int, double>> curve;
    for (int i = 0; i < 50; i++) curve.push_back({i * 100, 5.0 + sin(i * 0.1) * 3.0});
    gen.AddEquityCurve(curve);

    gen.ExportHTML(std::string(argv[2]) + ".html");
    gen.ExportJSON(std::string(argv[2]) + ".json");
    std::cout << "Dashboard exported to " << argv[2] << ".html/.json\n";
  } else {
    PrintUsage(argv[0]);
    return 1;
  }
  return 0;
}
