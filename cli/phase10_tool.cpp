#include <iomanip>
#include <iostream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/phase10/benchmark.h"
#include "poker_engine/phase10/parallel_batch_parser.h"
#include "poker_engine/phase10/parallel_cfr.h"
#include "poker_engine/phase10/parallel_preflop_lut.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase10;
using namespace poker_engine::range;
using namespace poker_engine::equity;

void PrintUsage(const char* prog) {
  std::cout << "Poker Phase 10 Tool v1.0 (Parallel Engine)\n"
            << "Usage: " << prog << " <command> [args...]\n\n"
            << "Commands:\n"
            << "  parallel-cfr <hero> <villain>     Parallel CFR solve\n"
            << "  speedup <hero> <villain>           Benchmark parallel vs serial\n"
            << "  build-lut [output_file]            Build pre-flop LUT\n"
            << "  query-lut <hand_a> <hand_b>        Query equity from LUT\n"
            << "  top-hands                          Show top hands by EV\n"
            << "  parse-parallel <dir>               Parallel HH parsing\n"
            << "  benchmark                          Run benchmarks\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }
  std::string cmd = argv[1];

  if (cmd == "parallel-cfr" && argc >= 4) {
    ParallelCFRConfig config;
    config.iterations = 2000;
    config.mc_samples_per_iter = 50;
    config.verbose = true;

    ParallelCCFRSolver solver(config);
    solver.SetHeroRange(Range::FromString(argv[2]));
    solver.SetVillainRange(Range::FromString(argv[3]));
    solver.SetPot(20, 7);

    auto result = solver.Solve();
    std::cout << "\n" << result.ToString() << "\n";

  } else if (cmd == "speedup" && argc >= 4) {
    ParallelCFRConfig config;
    config.iterations = 500;
    config.mc_samples_per_iter = 30;
    ParallelCCFRSolver solver(config);
    solver.SetHeroRange(Range::FromString(argv[2]));
    solver.SetVillainRange(Range::FromString(argv[3]));
    solver.SetPot(20, 7);
    solver.BenchmarkSpeedup();

  } else if (cmd == "build-lut") {
    PreflopLUTCalculator calc;
    auto lut = calc.Calculate(2000);
    std::string fp = (argc >= 3) ? argv[2] : "preflop_lut.bin";
    lut.SaveToFile(fp);
    std::cout << "LUT saved to: " << fp << " (" << sizeof(PreflopLUT) / 1024 << " KB)\n";

  } else if (cmd == "query-lut" && argc >= 4) {
    PreflopLUTCalculator calc(4);
    auto lut = calc.Calculate(500);
    float eq = PreflopLUTCalculator::QueryEquity(lut, argv[2], argv[3]);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << argv[2] << " vs " << argv[3] << ": " << eq * 100 << "% / " << (1 - eq) * 100
              << "%\n";

  } else if (cmd == "top-hands") {
    PreflopLUTCalculator calc(4);
    auto lut = calc.Calculate(500);
    auto ranked = PreflopLUTCalculator::TopHandsByPosition(lut, 20);
    std::cout << "\n=== Top 20 Hands (vs 1BB) ===\n";
    for (size_t i = 0; i < ranked.size(); i++)
      std::cout << "  " << std::setw(2) << (i + 1) << ". " << std::setw(6) << ranked[i].first << " "
                << std::fixed << std::setprecision(2) << ranked[i].second << " BB\n";

  } else if (cmd == "parse-parallel" && argc >= 3) {
    ParallelBatchParser parser(ParallelParseConfig{-1, ".txt", true, 50, true});
    auto stats = parser.ParseDirectory(argv[2]);
    std::cout << "\n" << stats.ToString() << "\n";

  } else if (cmd == "benchmark") {
    BenchmarkConfig cfg;
    cfg.warmup_iterations = 3;
    cfg.measure_iterations = 500;
    cfg.repeats = 3;
    BenchmarkRunner runner(cfg);

    runner.Register("RangeFromString", []() {}, []() { Range::FromString("AKs,QQ+,AKo"); });

    runner.Register(
        "EquityMC_2K", []() {},
        []() {
          Range hero = Range::FromString("AKs");
          Range villain = Range::FromString("22+");
          std::mt19937 rng(42);
          uint8_t b[5] = {0};
          EquityCalculator::CalculateMonteCarlo(hero, villain, b, 0, 2000, rng);
        });

    runner.Register(
        "PolarizedRange_1326", []() {},
        []() {
          Range base = Range::FromString("22+,A2s+");
          for (int i = 0; i < 1326; i++) {
            float w = base.Get(i);
            (void)w;
          }
        });

    auto results = runner.RunAll();
    std::cout << "\n=== Benchmark Summary ===\n";
    for (const auto& r : results) std::cout << r.ToString();

  } else {
    PrintUsage(argv[0]);
    return 1;
  }
  return 0;
}
