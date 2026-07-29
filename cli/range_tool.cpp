#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/phase2/range_builder.h"
#include "poker_engine/phase2/range_visualizer.h"
#include "poker_engine/phase2/solver_node.h"
#include "poker_engine/range/range.h"

using namespace poker_engine::phase2;
using poker_engine::Card;
using poker_engine::equity::EquityCalculator;
using poker_engine::range::HandId;
using poker_engine::range::Range;

void PrintUsage(const char* prog) {
  std::cout << "Poker Range Tool v0.2\n"
            << "Usage: " << prog << " <command> [args...]\n\n"
            << "Commands:\n"
            << "  show <position>     Show range heatmap\n"
            << "  compare <pos1> <pos2>  Compare two position ranges\n"
            << "  equity <range1> <range2>  Compute equity (no board)\n"
            << "  build <spec>         Build range from spec string\n"
            << "  viz-html <outfile>   Generate HTML range visualization\n"
            << "  solve               Run CFR solve demo\n";
}

void CmdShow(const std::string& pos) {
  RangeBuilder builder;
  Range r = builder.Build(pos);

  std::cout << "\n=== " << pos << "'s Open-Raise Range (" << r.NonZeroCount() << " combos) ===\n\n";

  RangeVisualizer viz(r);
  std::cout << viz.ToFullASCII() << "\n";
}

void CmdCompare(const std::string& pos1, const std::string& pos2) {
  RangeBuilder builder;
  Range r1 = builder.Build(pos1);
  Range r2 = builder.Build(pos2);

  std::cout << "\n=== Range Comparison ===\n";
  std::cout << pos1 << ": " << r1.NonZeroCount() << " combos\n";
  std::cout << pos2 << ": " << r2.NonZeroCount() << " combos\n";

  Range inter = r1 & r2;
  std::cout << "Intersection: " << inter.NonZeroCount() << " combos\n";

  Range diff = r1;
  diff -= r2;
  std::cout << pos1 << " only: " << diff.NonZeroCount() << " combos\n\n";
}

void CmdEquity(const std::string& spec1, const std::string& spec2) {
  Range r1 = Range::FromString(spec1);
  Range r2 = Range::FromString(spec2);

  std::mt19937 rng(42);
  uint8_t empty[1] = {0};
  auto result = EquityCalculator::CalculateMonteCarlo(r1, r2, empty, 0, 200000, rng);

  std::cout << "\n=== Equity: " << spec1 << " vs " << spec2 << " ===\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "P1 equity: " << result.equity[0] * 100 << "%\n";
  std::cout << "P2 equity: " << result.equity[1] * 100 << "%\n";
  std::cout << result.ToString() << "\n";
}

void CmdBuild(const std::string& spec) {
  Range r = Range::FromString(spec);
  std::cout << "\nRange: " << spec << "\n";
  std::cout << "Combos: " << r.NonZeroCount() << " / 1326\n";

  r.Normalize();
  std::cout << std::fixed << std::setprecision(4);

  std::vector<std::pair<uint16_t, float>> hands;
  for (int i = 0; i < 1326; i++) {
    float p = r.Get(i);
    if (p > 0) hands.push_back({(uint16_t)i, p});
  }
  std::sort(hands.begin(), hands.end(), [](auto& a, auto& b) { return a.second > b.second; });

  std::cout << "\nTop 10 hands by weight:\n";
  for (int i = 0; i < std::min(10, (int)hands.size()); i++) {
    auto [c1, c2] = HandId::Decode(hands[i].first);
    Card card1(c1), card2(c2);
    std::cout << "  " << card1.ToString() << " " << card2.ToString() << " = " << hands[i].second
              << "\n";
  }

  RangeVisualizer viz(r);
  std::cout << "\n" << viz.ToFullASCII();
}

void CmdHTML(const std::string& filepath) {
  RangeBuilder builder;
  Range r = builder.Build("BTN");

  RangeVisualizer viz(r);
  std::string html = viz.ToHTML();

  std::ofstream out(filepath);
  out << html;
  out.close();
  std::cout << "HTML heatmap saved to: " << filepath << "\n";
}

void CmdSolve() {
  std::cout << "\n=== CFR Solve Demo (AKs vs BB range, flop) ===\n\n";

  SolverConfig config;
  config.iterations = 500;
  config.n_samples = 1000;
  config.verbose = true;

  CFRSolver solver(config);

  RangeVector ranges;
  ranges.push_back(Range::FromString("AKs"));
  ranges.push_back(Range::FromString("22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+"));

  solver.SetRanges(ranges);
  solver.SetBoard({Card::Parse("Qd"), Card::Parse("Jh"), Card::Parse("7s")}, 1);

  auto result = solver.Solve();
  std::cout << "\n\n" << result.ToString() << "\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string cmd = argv[1];

  if (cmd == "show" && argc >= 3) {
    CmdShow(argv[2]);
  } else if (cmd == "compare" && argc >= 4) {
    CmdCompare(argv[2], argv[3]);
  } else if (cmd == "equity" && argc >= 4) {
    CmdEquity(argv[2], argv[3]);
  } else if (cmd == "build" && argc >= 3) {
    CmdBuild(argv[2]);
  } else if (cmd == "solve") {
    CmdSolve();
  } else if (cmd == "viz-html" && argc >= 3) {
    CmdHTML(argv[2]);
  } else {
    PrintUsage(argv[0]);
    return 1;
  }

  return 0;
}
