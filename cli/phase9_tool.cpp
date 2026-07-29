#include <fstream>
#include <iostream>
#include <string>

#include "poker_engine/phase9/advanced_batch_analyzer.h"
#include "poker_engine/phase9/pipeline_orchestrator.h"
#include "poker_engine/phase9/polarized_range.h"
#include "poker_engine/phase9/strategy_diff.h"
#include "poker_engine/phase9/variance_engine.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase9;
using namespace poker_engine::range;

void PrintUsage(const char* prog) {
  std::cout << "Poker Phase 9 Tool v0.9\n"
            << "Usage: " << prog << " <command> [args...]\n\n"
            << "Commands:\n"
            << "  variance <csv_file>           Variance/SWRR analysis\n"
            << "  strat-diff <range_a> <range_b>  Strategy difference\n"
            << "  polarize <range> [board]      Polarized range builder\n"
            << "  pipeline <dir> <hero>         Full pipeline\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }
  std::string cmd = argv[1];

  if (cmd == "variance" && argc >= 3) {
    VarianceEngine ve;
    std::ifstream f(argv[2]);
    if (!f.is_open()) {
      std::cerr << "Cannot open: " << argv[2] << "\n";
      return 1;
    }
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
      std::istringstream iss(line);
      std::string tok;
      double bb = 0;
      if (std::getline(iss, tok, ',') && std::getline(iss, tok, ',') && std::getline(iss, tok, ','))
        bb = std::stod(tok);
      ve.AddBB100(bb);
    }
    if (ve.Count() >= 2) {
      std::cout << ve.ComputeSummary().ToString() << "\n"
                << ve.ComputeSWRR().ToString() << "\n"
                << ve.ComputeHeatIndex().ToString();
      auto ci = ve.ConfidenceInterval(0.95);
      std::cout << "95% CI: [" << ci.first << ", " << ci.second << "]\n";
    }
  } else if (cmd == "strat-diff" && argc >= 4) {
    StrategyDiffAnalyzer a;
    auto s = a.CompareRanges(Range::FromString(argv[2]), Range::FromString(argv[3]));
    std::cout << s.ToString() << "\n";
  } else if (cmd == "polarize" && argc >= 3) {
    std::string board = (argc >= 4) ? argv[3] : "";
    auto r = PolarizedRangeBuilder::BuildFromString(argv[2], board, Polarity::POLARIZED);
    std::cout << "Polarized: " << r.ToString() << "\n";
    auto m = PolarizedRangeBuilder::BuildFromString(argv[2], board, Polarity::MERGED);
    std::cout << "Merged: " << m.ToString() << "\n";
  } else if (cmd == "pipeline" && argc >= 3) {
    PipelineConfig cfg;
    cfg.data_source = argv[2];
    cfg.hero_filter = (argc >= 4) ? argv[3] : "";
    AnalysisPipeline p(cfg);
    p.RunFullPipeline();
  } else {
    PrintUsage(argv[0]);
    return 1;
  }
  return 0;
}
