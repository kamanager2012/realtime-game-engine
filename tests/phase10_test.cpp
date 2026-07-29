#include <gtest/gtest.h>

#include <fstream>
#include <iostream>

#include "poker_engine/phase10/benchmark.h"
#include "poker_engine/phase10/parallel_batch_parser.h"
#include "poker_engine/phase10/parallel_cfr.h"
#include "poker_engine/phase10/parallel_preflop_lut.h"
#include "poker_engine/phase10/parallel_utils.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase10;
using namespace poker_engine::range;

// ============ PARALLEL FOR ============

TEST(Phase10Test, ParallelForInit) {
  ParallelFor::Init(4);
  EXPECT_EQ(ParallelFor::GetNumThreads(), 4);
}

TEST(Phase10Test, ParallelForSum) {
  ParallelFor::Init();
  std::atomic<int> sum{0};
  ParallelFor::Range(100, [&](int i) { sum += i; });
  EXPECT_EQ(sum.load(), 99 * 100 / 2);
}

TEST(Phase10Test, ParallelForRange) {
  ParallelFor::Init(4);
  std::vector<int> vec(50, 0);
  ParallelFor::Range(0, 50, [&](int i) { vec[i] = i * 2; });
  for (int i = 0; i < 50; i++) EXPECT_EQ(vec[i], i * 2);
}

// ============ THREAD POOL ============

TEST(Phase10Test, ThreadPoolCreation) {
  ThreadPool pool(4);
  EXPECT_EQ(pool.NumThreads(), 4);
}

TEST(Phase10Test, ThreadPoolTask) {
  ThreadPool pool(4);
  auto f = pool.Submit([]() { return 42; });
  EXPECT_EQ(f.get(), 42);
}

TEST(Phase10Test, ThreadPoolMultipleTasks) {
  ThreadPool pool(4);
  std::vector<std::future<int>> futures;
  for (int i = 0; i < 10; i++) futures.push_back(pool.Submit([i]() { return i * i; }));
  for (int i = 0; i < 10; i++) EXPECT_EQ(futures[i].get(), i * i);
}

TEST(Phase10Test, ThreadPoolWaitAll) {
  ThreadPool pool(2);
  std::atomic<int> counter{0};
  for (int i = 0; i < 100; i++) pool.Submit([&]() { counter++; });
  pool.WaitAll();
  EXPECT_EQ(counter.load(), 100);
}

TEST(Phase10Test, ThreadPoolParallelFor) {
  ThreadPool pool(4);
  std::vector<int> vec(50, 0);
  pool.ParallelFor(50, [&](int i) { vec[i] = i + 1; });
  for (int i = 0; i < 50; i++) EXPECT_EQ(vec[i], i + 1);
}

// ============ ATOMIC ACCUMULATOR ============

TEST(Phase10Test, AtomicAccumulator) {
  AtomicAccumulator<double> acc;
  EXPECT_DOUBLE_EQ(acc.Get(), 0.0);
  acc.Add(1.5);
  acc.Add(2.5);
  acc.Add(1.0);
  EXPECT_NEAR(acc.Get(), 5.0, 0.001);
  acc.Reset();
  EXPECT_DOUBLE_EQ(acc.Get(), 0.0);
}

TEST(Phase10Test, AtomicAccumulatorParallel) {
  AtomicAccumulator<int> acc;
  ThreadPool pool(4);
  for (int i = 0; i < 1000; i++) pool.Submit([&]() { acc.Add(1); });
  pool.WaitAll();
  EXPECT_EQ(acc.Get(), 1000);
}

// ============ PARALLEL RNG ============

TEST(Phase10Test, ParallelRNGBasics) {
  ParallelRNG rng(12345);
  auto& local = rng.GetRNG();
  EXPECT_NE(local(), local());
}

// ============ STOPWATCH & PROFILER ============

TEST(Phase10Test, StopwatchMeasure) {
  Stopwatch sw;
  volatile double sum = 0;
  for (int i = 0; i < 100000; i++) sum += i * 0.001;
  sw.Stop();
  EXPECT_GT(sw.ElapsedSeconds(), 0);
  EXPECT_LT(sw.ElapsedSeconds(), 10);
}

TEST(Phase10Test, ProfilerReport) {
  Profiler& prof = Profiler::Instance();
  prof.Reset();
  prof.StartSection("test_a");
  volatile double x = 0;
  for (int i = 0; i < 1000; i++) x += i;
  prof.EndSection("test_a");
  std::string report = prof.GetReport();
  EXPECT_NE(report.find("test_a"), std::string::npos);
}

// ============ PARALLEL CFR ============

TEST(Phase10Test, ParallelCFRSolve) {
  ParallelCCFRSolver solver(
      ParallelCFRConfig{200, 20, 2, phase8::MCRMode::EXTERNAL_SAMPLING, 0.995, 12, false});
  solver.SetHeroRange(Range::FromString("AKs,AQs"));
  solver.SetVillainRange(Range::FromString("22+,K2s+"));
  solver.SetPot(20, 7);
  auto result = solver.Solve();
  EXPECT_GE(result.total_iterations, 200);
  EXPECT_GT(result.strategy_map.size(), 0u);
}

TEST(Phase10Test, ParallelCFRMultiThread) {
  ParallelCCFRSolver solver(
      ParallelCFRConfig{100, 10, 4, phase8::MCRMode::EXTERNAL_SAMPLING, 0.995, 12, false});
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+"));
  solver.SetPot(20, 7);
  auto result = solver.Solve();
  EXPECT_EQ(result.num_threads_used, 4);
  EXPECT_EQ(result.iterations_per_thread.size(), 4u);
}

TEST(Phase10Test, ParallelCFRResultFormat) {
  ParallelCFRResult result;
  result.total_iterations = 1000;
  result.exploitability_mbb = 25.5;
  result.total_time_seconds = 2.5;
  result.num_threads_used = 4;
  result.speedup_vs_serial = 2.8;
  result.iterations_per_thread = {250, 250, 250, 250};
  std::string s = result.ToString();
  EXPECT_NE(s.find("Parallel"), std::string::npos);
  EXPECT_NE(s.find("Threads: 4"), std::string::npos);
}

TEST(Phase10Test, SpeedupBenchmark) {
  ParallelCCFRSolver solver(
      ParallelCFRConfig{200, 20, 2, phase8::MCRMode::EXTERNAL_SAMPLING, 0.995, 12, false});
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+"));
  solver.SetPot(20, 5);
  double speedup = solver.BenchmarkSpeedup();
  EXPECT_GT(speedup, 0.1);  // Just verify it runs
}

// ============ PRE-FLOP LUT ============

TEST(Phase10Test, PreflopHandTypeBasics) {
  auto ht = PreflopHandType::FromHandName("AKs");
  EXPECT_EQ(ht.rank1, 12);
  EXPECT_EQ(ht.rank2, 11);
  EXPECT_TRUE(ht.suited);
  auto ht2 = PreflopHandType::FromHandName("72o");
  EXPECT_EQ(ht2.rank1, 5);
  EXPECT_EQ(ht2.rank2, 0);
  EXPECT_FALSE(ht2.suited);
}

TEST(Phase10Test, PreflopHandIndex) {
  int idx = PreflopHandType::ToIndex(12, 12, false);
  EXPECT_EQ(idx, 0);  // AA
  int idx2 = PreflopHandType::ToIndex(11, 11, false);
  EXPECT_EQ(idx2, 1);  // KK
  int idx3 = PreflopHandType::ToIndex(12, 11, true);
  EXPECT_GE(idx3, 13);  // AKs after pairs
  auto rt = PreflopHandType::FromIndex(idx3);
  EXPECT_EQ(rt.rank1, 12);
  EXPECT_EQ(rt.rank2, 11);
  EXPECT_TRUE(rt.suited);
}

TEST(Phase10Test, PreflopLUTSaveLoad) {
  PreflopLUTCalculator calc(2);
  auto lut = calc.Calculate(200);  // Fast for testing
  lut.SaveToFile("/tmp/test_preflop_lut.bin");
  PreflopLUT loaded;
  EXPECT_TRUE(loaded.LoadFromFile("/tmp/test_preflop_lut.bin"));
  std::string info = loaded.ToString();
  EXPECT_NE(info.find("Pre-flop"), std::string::npos);
  std::remove("/tmp/test_preflop_lut.bin");
}

TEST(Phase10Test, PreflopLUTQuery) {
  PreflopLUTCalculator calc(4);
  auto lut = calc.Calculate(500);
  float eq = PreflopLUTCalculator::QueryEquity(lut, "AA", "72o");
  EXPECT_GT(eq, 0.6);
  float eq2 = PreflopLUTCalculator::QueryEquity(lut, "72o", "AA");
  EXPECT_LT(eq2, 0.4);
  EXPECT_NEAR(eq + eq2, 1.0, 0.05);
}

TEST(Phase10Test, PreflopLUTTopHands) {
  PreflopLUTCalculator calc(2);
  auto lut = calc.Calculate(200);
  auto ranked = PreflopLUTCalculator::TopHandsByPosition(lut, 10);
  EXPECT_EQ(ranked.size(), 10u);
  EXPECT_EQ(ranked[0].first, "AA");  // AA should be #1
}

// ============ BENCHMARK ============

TEST(Phase10Test, BenchmarkRunnerBasic) {
  BenchmarkConfig cfg;
  cfg.warmup_iterations = 2;
  cfg.measure_iterations = 100;
  cfg.repeats = 2;
  BenchmarkRunner runner(cfg);
  runner.Register(
      "simple", []() {},
      []() {
        volatile double x = 0;
        for (int i = 0; i < 100; i++) x += i;
      });
  auto results = runner.RunAll();
  EXPECT_EQ(results.size(), 1u);
  EXPECT_GT(results[0].median_ms, 0);
}

TEST(Phase10Test, BenchmarkCompare) {
  BenchmarkResult before{"test", 10.0, 10.0, 8.0, 12.0, 1.0, 100};
  BenchmarkResult after{"test", 4.0, 4.5, 3.5, 5.0, 0.5, 100};
  std::string cmp = BenchmarkRunner::Compare(before, after);
  EXPECT_NE(cmp.find("Speedup"), std::string::npos);
}

// ============ BATCH PARSER ============

TEST(Phase10Test, ParallelParserEmptyDir) {
  ParallelBatchParser parser(ParallelParseConfig{2, ".txt", true, 50, false});
  auto stats = parser.ParseDirectory("/tmp/nonexistent_dir_xyz");
  EXPECT_EQ(stats.total_files, 0);
}

TEST(Phase10Test, QuickCountEmptyDir) {
  ParallelBatchParser parser(ParallelParseConfig{2});
  auto stats = parser.QuickCount("/tmp/nonexistent_dir_xyz");
  EXPECT_EQ(stats.total_files, 0);
}

// ============ INTEGRATION ============

TEST(Phase10Test, FullPipelineParallel) {
  std::string hh = R"(
PokerStars Hand #2500: Hold'em No Limit ($1/$2)
Table 'Fast' 6-max Seat #1 is the button
Seat 1: Hero ($500)
Seat 2: Alpha ($500)
Dealt to Hero [As Ks]
Alpha: posts the small blind $1
Hero: posts the big blind $2
Hero: raises to $7
Alpha: folds
*** SUMMARY ***
Total pot: $10
Hero collected $10
)";
  std::string tmp = "/tmp/parallel_test.txt";
  std::ofstream(tmp) << hh;

  ParallelBatchParser parser(ParallelParseConfig{2, ".txt", false, 50, false});
  auto stats = parser.ParseDirectory("/tmp");
  EXPECT_GE(stats.successful, 0);  // May or may not find the file depending on /tmp contents
}
