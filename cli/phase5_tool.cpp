#include <iostream>
#include <string>

#include "poker_engine/phase5/bulk_hh_parser.h"
#include "poker_engine/phase5/equity_matrix.h"
#include "poker_engine/phase5/hand_generator.h"
#include "poker_engine/phase5/icm_calc.h"
#include "poker_engine/phase5/regression_analyzer.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase5;
using namespace poker_engine::range;

void PrintUsage(const char* prog) {
  std::cout << "Poker Phase 5 Tool v0.5\n"
            << "Usage: " << prog << " <command> [args...]\n\n"
            << "Commands:\n"
            << "  equity-matrix <hero> <villain>  Compute equity matrix\n"
            << "  icm <payouts> <chips>            Calculate ICM\n"
            << "  icm-pushfold <stack_bb> <eff_bb> Push/fold table\n"
            << "  regression <bb_values>          Analyze regression trend\n"
            << "  generate <n>                     Generate random hands\n"
            << "  distribution <n>                 Hand distribution stats\n"
            << "  bulk-parse <dir>                 Parse HH directory\n";
}

void CmdEquityMatrix(const std::string& hero, const std::string& villain) {
  EquityMatrixCalculator calc;

  auto result =
      calc.Calculate(std::vector<std::string>{hero}, std::vector<std::string>{villain}, 10000);

  std::cout << "\n" << result.ToString() << "\n";
}

void CmdICM(const std::vector<double>& payouts, const std::vector<double>& chips) {
  auto result =
      ICMCalculator::Calculate(payouts.data(), payouts.size(), chips.data(), chips.size());

  std::cout << result.ToString() << "\n";
}

void CmdPushFold(double stack_bb, double eff_bb) {
  auto table = ICMCalculator::PushFoldTable(stack_bb, eff_bb);

  std::cout << "\n=== Push/Fold Table (Hero " << stack_bb << "BB vs " << eff_bb << "BB) ===\n\n";
  std::cout << "Push hands:\n";
  for (const auto& entry : table) {
    if (entry.push) {
      std::cout << "  " << entry.hand_name << " (eq_needed=" << int(entry.equity_needed * 100)
                << "%"
                << ", ev_push=" << entry.ev_push << "BB)\n";
    }
  }
}

void CmdRegression(const std::vector<double>& bb_values) {
  RegressionAnalyzer analyzer;
  for (double v : bb_values) analyzer.AddProfit(v);

  std::cout << analyzer.TrendReport(100) << "\n";
}

void CmdGenerate(int n) {
  HandGenerator gen(42);
  HandGenConfig config;
  config.num_players = 6;
  config.big_blind = 1.0;

  auto hands = gen.GenerateBatch(n, config);

  std::cout << "\nGenerated " << n << " hands:\n";
  for (int i = 0; i < std::min(n, 5); i++) {
    std::cout << "  Hand " << i + 1 << ": Board=" << hands[i].board << "\n";
    for (int p = 0; p < 2; p++) {
      std::cout << "    " << hands[i].player_names[p] << ": " << hands[i].hole_cards[p] << "\n";
    }
  }
  if (n > 5) std::cout << "  ... and " << (n - 5) << " more\n";
}

void CmdDistribution(int n) {
  HandGenerator gen(42);
  auto dist = gen.ComputeDistribution(n, 6);
  std::cout << dist.ToString() << "\n";
}

void CmdBulkParse(const std::string& dir) {
  BulkParseConfig config;
  config.file_extension = ".txt";

  BulkHandHistoryParser parser(config);
  auto stats = parser.ParseDirectory(dir);
  std::cout << stats.ToString() << "\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string cmd = argv[1];

  if (cmd == "equity-matrix" && argc >= 4) {
    CmdEquityMatrix(argv[2], argv[3]);
  } else if (cmd == "icm" && argc >= 3) {
    std::vector<double> payouts, chips;
    // Simplified: just 2-player ICM with default payouts
    double p1 = 100.0, p2 = 60.0;
    payouts = {p1, p2};
    chips = {std::stod(argv[2]), argc >= 4 ? std::stod(argv[3]) : 6000.0};
    CmdICM(payouts, chips);
  } else if (cmd == "icm-pushfold" && argc >= 4) {
    CmdPushFold(std::stod(argv[2]), std::stod(argv[3]));
  } else if (cmd == "regression" && argc >= 3) {
    std::vector<double> values;
    for (int i = 2; i < argc; i++) values.push_back(std::stod(argv[i]));
    CmdRegression(values);
  } else if (cmd == "generate" && argc >= 3) {
    CmdGenerate(std::stoi(argv[2]));
  } else if (cmd == "distribution" && argc >= 3) {
    CmdDistribution(std::stoi(argv[2]));
  } else if (cmd == "bulk-parse" && argc >= 3) {
    CmdBulkParse(argv[2]);
  } else {
    PrintUsage(argv[0]);
    return 1;
  }

  return 0;
}
