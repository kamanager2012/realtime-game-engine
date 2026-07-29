#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "poker_engine/phase7/cfr_plus_solver.h"
#include "poker_engine/phase7/mtt_simulator.h"
#include "poker_engine/phase7/opponent_modeler.h"
#include "poker_engine/phase7/tournament_icm.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase7;
using namespace poker_engine::range;

void PrintUsage(const char* prog) {
  std::cout << "Poker Phase 7 Tool v0.7\n"
            << "Usage: " << prog << " <command> [args...]\n\n"
            << "Commands:\n"
            << "  cfr-plus <hero> <villain> [flop]   Solve CFR+ (DCFR)\n"
            << "  cfr-compare <hero> <villain>      Compare CFR variants\n"
            << "  icm <payouts> <stacks>            Tournament ICM\n"
            << "  icm-pushfold <bb> <stack>         Push/fold table\n"
            << "  icm-bubble <bb> <total>           Bubble analysis\n"
            << "  model <dir> <player>              Opponent modeling\n"
            << "  mtt-sim <n>                       MTT simulation\n";
}

void CmdCFRSolve(const std::string& hero_str, const std::string& villain_str,
                 const std::string& flop_str) {
  CFRConfig config;
  config.iterations = 2000;
  config.mc_samples = 1000;
  config.mode = CFRMode::DISCOUNTED;
  config.alpha = 1.5;
  config.beta = 0.5;
  config.gamma = 2.0;
  config.verbose = true;
  CFRPlusSolver solver(config);
  solver.SetHeroRange(Range::FromString(hero_str));
  solver.SetVillainRange(Range::FromString(villain_str));
  solver.SetPot(20);
  solver.SetToCall(5);
  if (!flop_str.empty()) {
    std::vector<Card> board;
    for (size_t i = 0; i + 1 < flop_str.size(); i += 2)
      board.push_back(Card::Parse(flop_str.substr(i, 2)));
    solver.SetBoard(board);
  }
  auto result = solver.Solve();
  std::cout << "\n" << result.ToString() << "\n";
}

void CmdCFRCompare(const std::string& hero_str, const std::string& villain_str) {
  std::vector<std::pair<CFRMode, std::string>> modes = {
      {CFRMode::VANILLA, "Vanilla CFR"},
      {CFRMode::CHANCE_SAMPLED, "Chance-Sampled CFR"},
      {CFRMode::DISCOUNTED, "Discounted CFR (DCFR)"}};
  std::cout << "\n=== CFR Variant Comparison ===\n\n";
  for (const auto& [mode, name] : modes) {
    CFRConfig config;
    config.iterations = 500;
    config.mc_samples = 500;
    config.mode = mode;
    config.verbose = false;
    CFRPlusSolver solver(config);
    solver.SetHeroRange(Range::FromString(hero_str));
    solver.SetVillainRange(Range::FromString(villain_str));
    solver.SetPot(20);
    solver.SetToCall(7);
    auto result = solver.Solve();
    std::cout << name << ":\n  Time: " << result.time_seconds
              << "s | Exploitability: " << result.exploitability
              << " mBB | Nodes: " << result.strategy_profile.size() << "\n\n";
  }
}

void CmdICM(const std::string& payouts_str, const std::string& stacks_str) {
  auto parse = [](const std::string& s) -> std::vector<double> {
    std::vector<double> v;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, ',')) v.push_back(std::stod(token));
    return v;
  };
  auto payouts = parse(payouts_str);
  auto stacks = parse(stacks_str);
  if (payouts.size() < 2 || stacks.size() < 2) {
    std::cerr << "Need at least 2 payouts and 2 stacks\n";
    return;
  }
  TournamentICM icm;
  TourneyConfig config;
  config.total_starting = stacks.size();
  config.players_remaining = config.total_starting;
  config.total_prize_pool = std::accumulate(payouts.begin(), payouts.end(), 0.0);
  icm.SetConfig(config);
  icm.SetPayoutSchedule(payouts);
  for (size_t i = 0; i < stacks.size(); i++) icm.AddPlayer("P" + std::to_string(i + 1), stacks[i]);
  auto result = icm.Calculate();
  std::cout << result.ToString() << "\n";
  auto analysis = icm.AnalyzeBubble(stacks.back(), static_cast<int>(stacks.size()) - 1);
  std::cout << "\nBubble Analysis (shortest stack=" << (int)stacks.back()
            << "):\n  Elim: " << analysis.elimination_probability * 100
            << "% | Doublings: " << analysis.expected_doublings
            << "\n  Suggested: " << analysis.suggested_action << "\n";
}

void CmdPushFold(double bb, double eff_stack) {
  TournamentICM icm;
  TourneyConfig config;
  config.big_blind = bb;
  icm.SetConfig(config);
  auto table = icm.PushFoldTable(eff_stack, bb, false);
  std::cout << "\n=== Push/Fold Table (BB=" << bb << ", EffStack=" << eff_stack << " BB) ===\n\n";
  for (const auto& advice : table) std::cout << "  " << advice.ToString() << "\n";
  std::cout << "\nNash Push Range (M=" << eff_stack
            << "): " << icm.NashPushRange(eff_stack, bb) * 100 << "%\n";
}

void CmdMTTSim(int num_players) {
  MTTConfig config;
  config.target_players = num_players;
  config.num_tables = (num_players + 8) / 9;
  config.starting_stack = 1500;
  config.has_rebuys = true;
  config.max_rebuys = 1;
  config.rebuy_cost = 0;
  config.rebuy_stack = 1500;
  config.payout_percentages = {50, 30, 20};
  double blinds[][3] = {{10, 20, 0},       {15, 30, 0},       {25, 50, 0},      {50, 100, 0},
                        {75, 150, 0},      {100, 200, 25},    {150, 300, 25},   {200, 400, 25},
                        {300, 600, 50},    {400, 800, 50},    {600, 1200, 100}, {800, 1600, 100},
                        {1000, 2000, 100}, {1500, 3000, 200}, {2000, 4000, 200}};
  for (int i = 0; i < 15; i++) {
    BlindLevel level;
    level.level = i + 1;
    level.small_blind = blinds[i][0];
    level.big_blind = blinds[i][1];
    level.ante = blinds[i][2];
    level.duration_minutes = 15;
    config.blind_schedule.push_back(level);
  }
  MTTSimulator sim(config);
  for (int i = 0; i < num_players; i++) sim.SetPlayerName(i + 1, "Bot_" + std::to_string(i + 1));
  sim.SetPlayerName(1, "Hero");
  sim.SetAllRandomStrategies();
  auto result = sim.Run();
  std::cout << result.ToString() << "\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }
  std::string cmd = argv[1];
  if (cmd == "cfr-plus" && argc >= 4) {
    CmdCFRSolve(argv[2], argv[3], argc >= 5 ? argv[4] : "");
  } else if (cmd == "cfr-compare" && argc >= 4) {
    CmdCFRCompare(argv[2], argv[3]);
  } else if (cmd == "icm" && argc >= 4) {
    CmdICM(argv[2], argv[3]);
  } else if (cmd == "icm-pushfold" && argc >= 4) {
    CmdPushFold(std::stod(argv[2]), std::stod(argv[3]));
  } else if (cmd == "icm-bubble" && argc >= 4) {
    TournamentICM icm;
    TourneyConfig cfg;
    cfg.big_blind = std::stod(argv[2]);
    cfg.total_starting = std::stoi(argv[3]);
    cfg.players_remaining = cfg.total_starting;
    cfg.total_prize_pool = cfg.total_starting * cfg.big_blind * 100;
    icm.SetConfig(cfg);
    for (int i = 0; i < cfg.total_starting; i++)
      icm.AddPlayer("P" + std::to_string(i + 1), cfg.big_blind * (10 + i));
    auto result = icm.Calculate();
    std::cout << result.ToString() << "\n";
  } else if (cmd == "model" && argc >= 3) {
    OpponentModeler modeler;
    std::string player = (argc >= 4) ? argv[3] : "";
    if (player.empty())
      std::cout << modeler.SessionReport();
    else
      std::cout << modeler.PlayerReport(player);
  } else if (cmd == "mtt-sim" && argc >= 3) {
    CmdMTTSim(std::stoi(argv[2]));
  } else {
    PrintUsage(argv[0]);
    return 1;
  }
  return 0;
}
