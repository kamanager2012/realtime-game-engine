#include <algorithm>
#include <chrono>
#include <numeric>

#include "poker_engine/phase10/benchmark.h"

namespace poker_engine {
namespace phase10 {

BenchmarkRunner::BenchmarkRunner(const BenchmarkConfig& config) : config_(config) {}

void BenchmarkRunner::Register(const std::string& name, std::function<void()> setup,
                               std::function<void()> bench) {
  entries_.push_back({name, std::move(setup), std::move(bench)});
}

std::vector<BenchmarkResult> BenchmarkRunner::RunAll() {
  std::vector<BenchmarkResult> results;
  for (const auto& e : entries_) {
    results.push_back(RunOne(e.name, e.setup, e.bench));
  }
  return results;
}

BenchmarkResult BenchmarkRunner::RunOne(const std::string& name, std::function<void()> setup,
                                        std::function<void()> bench) {
  // Warmup
  for (int i = 0; i < config_.warmup_iterations; ++i) {
    if (setup) setup();
    bench();
  }

  // Measure across repeats
  std::vector<double> repeat_medians;
  for (int r = 0; r < config_.repeats; ++r) {
    if (setup) setup();
    std::vector<double> samples;
    samples.reserve(config_.measure_iterations);
    for (int i = 0; i < config_.measure_iterations; ++i) {
      auto start = std::chrono::high_resolution_clock::now();
      bench();
      auto end = std::chrono::high_resolution_clock::now();
      double ms = std::chrono::duration<double, std::milli>(end - start).count();
      samples.push_back(ms);
    }
    std::sort(samples.begin(), samples.end());
    repeat_medians.push_back(samples[samples.size() / 2]);
  }

  // Aggregate
  std::sort(repeat_medians.begin(), repeat_medians.end());
  double median = repeat_medians[repeat_medians.size() / 2];
  double sum = std::accumulate(repeat_medians.begin(), repeat_medians.end(), 0.0);
  double mean = sum / repeat_medians.size();
  double min_val = repeat_medians.front();
  double max_val = repeat_medians.back();
  double sq_sum = 0.0;
  for (double v : repeat_medians) sq_sum += (v - mean) * (v - mean);
  double stddev = std::sqrt(sq_sum / repeat_medians.size());

  return {
      name, median, mean, min_val, max_val, stddev, config_.measure_iterations * config_.repeats};
}

std::string BenchmarkResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3);
  oss << "  " << name << ": median=" << median_ms << "ms  mean=" << mean_ms << "ms  min=" << min_ms
      << "ms  max=" << max_ms << "ms  stddev=" << stddev_ms << "ms  (n=" << iterations << ")\n";
  return oss.str();
}

std::string BenchmarkRunner::Compare(const BenchmarkResult& before, const BenchmarkResult& after) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  double ratio = after.median_ms / before.median_ms;
  double pct = (ratio - 1.0) * 100.0;
  oss << before.name << " → " << after.name << ": ";
  if (pct < 0)
    oss << pct << "% faster";
  else if (pct > 0)
    oss << "+" << pct << "% slower";
  else
    oss << "no change";
  oss << " (" << before.median_ms << "ms → " << after.median_ms << "ms)\n";
  return oss.str();
}

}  // namespace phase10
}  // namespace poker_engine
