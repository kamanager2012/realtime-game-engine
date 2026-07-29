#pragma once
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace poker_engine {
namespace phase10 {

struct BenchmarkConfig {
  std::string name;
  int warmup_iterations = 10;
  int measure_iterations = 100;
  int repeats = 5;
};

struct BenchmarkResult {
  std::string name;
  double median_ms = 0, mean_ms = 0, min_ms = 0, max_ms = 0, stddev_ms = 0;
  int iterations = 0;
  std::string ToString() const;
};

class BenchmarkRunner {
 public:
  explicit BenchmarkRunner(const BenchmarkConfig& config = BenchmarkConfig{});
  void Register(const std::string& name, std::function<void()> setup, std::function<void()> bench);
  std::vector<BenchmarkResult> RunAll();
  BenchmarkResult RunOne(const std::string& name, std::function<void()> setup,
                         std::function<void()> bench);
  static std::string Compare(const BenchmarkResult& before, const BenchmarkResult& after);

 private:
  BenchmarkConfig config_;
  struct Entry {
    std::string name;
    std::function<void()> setup, bench;
  };
  std::vector<Entry> entries_;
};

}  // namespace phase10
}  // namespace poker_engine
