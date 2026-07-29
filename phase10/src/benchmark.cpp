#include "poker_engine/phase10/benchmark.h"

#include <algorithm>
#include <iostream>
#include <numeric>

namespace poker_engine {
namespace phase10 {

std::string BenchmarkResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3);
  oss << name << ": median=" << median_ms << "ms mean=" << mean_ms << "ms min=" << min_ms
      << "ms max=" << max_ms << "ms\n";
  return oss.str();
}

BenchmarkRunner::BenchmarkRunner(const BenchmarkConfig& config) : config_(config) {}

void BenchmarkRunner::Register(const std::string& name, std::function<void()> setup,
                               std::function<void()> bench) {
  entries_.push_back({name, setup, bench});
}

BenchmarkResult BenchmarkRunner::RunOne(const std::string& name, std::function<void()> setup,
                                        std::function<void()> bench) {
  BenchmarkResult result;
  result.name = name;
  setup();
  for (int i = 0; i < config_.warmup_iterations; i++) bench();
  std::vector<double> times;
  for (int r = 0; r < config_.repeats; r++) {
    setup();
    auto s = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < config_.measure_iterations; i++) bench();
    auto e = std::chrono::high_resolution_clock::now();
    times.push_back(std::chrono::duration<double, std::milli>(e - s).count() /
                    config_.measure_iterations);
  }
  result.iterations = config_.measure_iterations;
  std::sort(times.begin(), times.end());
  result.min_ms = times.front();
  result.max_ms = times.back();
  result.median_ms = times[times.size() / 2];
  result.mean_ms = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
  double sq = 0;
  for (double t : times) sq += (t - result.mean_ms) * (t - result.mean_ms);
  result.stddev_ms = std::sqrt(sq / times.size());
  return result;
}

std::vector<BenchmarkResult> BenchmarkRunner::RunAll() {
  std::vector<BenchmarkResult> results;
  for (const auto& e : entries_) {
    std::cout << "Benchmark: " << e.name << "...\n";
    results.push_back(RunOne(e.name, e.setup, e.bench));
  }
  return results;
}

std::string BenchmarkRunner::Compare(const BenchmarkResult& before, const BenchmarkResult& after) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  double speedup = before.median_ms / std::max(after.median_ms, 0.001);
  oss << "Before: " << before.median_ms << "ms | After: " << after.median_ms
      << "ms | Speedup: " << speedup << "x\n";
  return oss.str();
}

}  // namespace phase10
}  // namespace poker_engine
