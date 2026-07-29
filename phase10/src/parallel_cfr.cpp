#include "poker_engine/phase10/parallel_cfr.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"

namespace poker_engine {
namespace phase10 {
using namespace poker_engine::range;
using namespace poker_engine::equity;
using namespace poker_engine::evaluator;

std::string ParallelCFRResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(4);
  oss << "=== Parallel MC-CFR Deep Results ===\n";
  oss << "Iterations: " << total_iterations << "\n";
  oss << "Threads: " << num_threads_used << "\n";
  oss << "Time: " << total_time_seconds << "s\n";
  oss << "Speedup: " << speedup_vs_serial << "x\n";
  oss << "InfoSets: " << strategy_map.size() << "\n";
  oss << "Exploitability: " << exploitability_mbb << " mBB\n";
  if (!iterations_per_thread.empty()) {
    oss << "\n--- Thread Load Balance ---\n";
    for (size_t t = 0; t < iterations_per_thread.size(); t++)
      oss << "  Thread " << t << ": " << iterations_per_thread[t] << " iters\n";
  }
  return oss.str();
}

std::string ParallelCFRResult::SpeedupReport() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "Speedup: " << speedup_vs_serial << "x (" << num_threads_used << " threads, "
      << total_time_seconds << "s)\n";
  oss << "Efficiency: " << (speedup_vs_serial / std::max(num_threads_used, 1) * 100) << "%\n";
  return oss.str();
}

ParallelCCFRSolver::ParallelCCFRSolver(const ParallelCFRConfig& config) : config_(config) {}

void ParallelCCFRSolver::SetHeroRange(const Range& r) { hero_range_ = r; }
void ParallelCCFRSolver::SetVillainRange(const Range& r) { villain_range_ = r; }
void ParallelCCFRSolver::SetPot(double pot, double to_call) {
  pot_ = pot;
  to_call_ = to_call;
}
void ParallelCCFRSolver::SetBoard(const std::vector<Card>& board) {
  board_.clear();
  for (const auto& c : board) board_.push_back(c);
}

double ParallelCCFRSolver::ComputeEquityParallel(int n_samples) {
  if (n_samples <= 0) n_samples = 10;
  uint8_t b5[5] = {0};
  for (size_t i = 0; i < board_.size() && i < 5; i++) b5[i] = board_[i].Id();
  int bs = static_cast<int>(board_.size());

  // Use thread-local RNG
  static thread_local std::mt19937 local_rng(
      std::hash<std::thread::id>{}(std::this_thread::get_id()) + 7919);

  Range hero_local = hero_range_;
  Range villain_local = villain_range_;
  auto res = EquityCalculator::CalculateMonteCarlo(hero_local, villain_local, b5, bs, n_samples,
                                                   local_rng);
  return res.equity[0];
}

double ParallelCCFRSolver::CFRPartial(int thread_id, int player, double reach_hero,
                                      double reach_villain, int street_idx,
                                      const std::string& history, int depth) {
  if (depth > config_.max_depth) return 0;
  double min_reach = std::min(reach_hero, reach_villain);
  if (min_reach < 1e-10) return 0;

  // Terminal: fold
  if (!history.empty() && history.back() == 'f') {
    return (player == 0) ? -pot_ * 0.3 : pot_ * 0.3;
  }

  // Showdown
  if (history.length() >= 6 || depth >= 4) {
    double equity = ComputeEquityParallel(config_.mc_samples_per_iter / 10);
    double showdown_ev = equity * pot_ - (1.0 - equity) * to_call_;
    return (player == 0) ? showdown_ev : -showdown_ev;
  }

  MCInfoSetKey key{street_idx, history, pot_, to_call_, depth / 3};

  MCNode* node_ptr = nullptr;
  {
    std::lock_guard<std::mutex> lock(node_map_mutex_);
    auto& node = node_map_[key];
    node.visit_count++;
    node_ptr = &node;
  }
  MCNode& node = *node_ptr;

  double strategy[NUM_MC_ACTIONS];
  node.GetRegretMatchedStrategy(strategy);

  double util[NUM_MC_ACTIONS] = {0};
  double node_util = 0;
  double equity = ComputeEquityParallel(config_.mc_samples_per_iter / 20);

  for (int a = 0; a < NUM_MC_ACTIONS; a++) {
    if (static_cast<poker_engine::phase8::MCCAction>(a) == poker_engine::phase8::MCCAction::FOLD) {
      util[a] = -(to_call_ * 0.5);
    } else if (static_cast<poker_engine::phase8::MCCAction>(a) ==
                   poker_engine::phase8::MCCAction::CHECK ||
               static_cast<poker_engine::phase8::MCCAction>(a) ==
                   poker_engine::phase8::MCCAction::CALL) {
      util[a] = equity * pot_ - (1.0 - equity) * to_call_;
    } else {
      double cost = pot_ * (0.33 * std::max(1, a - 2));
      util[a] = equity * (pot_ + cost) - (1.0 - equity) * cost;
    }
    node_util += strategy[a] * util[a];
  }

  // Regret update with discount
  double alpha = std::pow(static_cast<double>(config_.iterations) / (config_.iterations + 1),
                          config_.discount_factor);
  for (int a = 0; a < NUM_MC_ACTIONS; a++) {
    double regret = (util[a] - node_util) * reach_villain;
    if (regret > 0) node.cumulative_regret[a] += regret * alpha;
  }
  for (int a = 0; a < NUM_MC_ACTIONS; a++) {
    node.strategy_sum[a] += strategy[a] * reach_hero;
  }

  // Recurse: hero traverses all actions, opponent sampled
  double total_util = 0;
  bool is_hero = (history.length() % 2 == 0);

  if (is_hero) {
    for (int a = 0; a < NUM_MC_ACTIONS; a++) {
      if (strategy[a] < 1e-8) continue;
      std::string new_hist = history + poker_engine::phase8::MCActionName[a][0];
      total_util += strategy[a] * CFRPartial(thread_id, player, reach_hero * strategy[a],
                                             reach_villain, street_idx, new_hist, depth + 1);
    }
  } else {
    // External sampling: sample one opponent action
    static thread_local std::mt19937 local_rng(
        std::hash<std::thread::id>{}(std::this_thread::get_id()) + 42);
    std::uniform_int_distribution<int> dist(0, NUM_MC_ACTIONS - 1);
    int sampled_a = dist(local_rng);
    std::string new_hist = history + poker_engine::phase8::MCActionName[sampled_a][0];
    total_util = (1.0 / NUM_MC_ACTIONS) * CFRPartial(thread_id, player, reach_hero,
                                                     reach_villain * (1.0 / NUM_MC_ACTIONS),
                                                     street_idx, new_hist, depth + 1);
  }
  return total_util;
}

void ParallelCCFRSolver::SolveThread(int thread_id, std::atomic<int>& global_iter,
                                     std::vector<double>& thread_ev) {
  int local_iters = 0;
  double local_ev = 0;
  while (true) {
    int iter = global_iter.fetch_add(1, std::memory_order_relaxed);
    if (iter >= config_.iterations) break;
    for (int p = 0; p < 2; p++) {
      double ev = CFRPartial(thread_id, p, 1.0, 1.0, 0, "", 0);
      if (p == 0) local_ev += ev;
    }
    local_iters++;
    if (config_.verbose && (iter + 1) % 500 == 0) {
      std::cout << "\r[T" << thread_id << "] iter " << (iter + 1) << "/" << config_.iterations
                << " | nodes=" << node_map_.size() << std::flush;
    }
  }
  iterations_per_thread_[thread_id] = local_iters;
  thread_ev[thread_id] = local_ev;
}

ParallelCFRResult ParallelCCFRSolver::Solve() {
  auto start_time = std::chrono::high_resolution_clock::now();
  ParallelCFRResult result;

  if (hero_range_.NonZeroCount() == 0) hero_range_ = Range::FullCombinatorial();
  if (villain_range_.NonZeroCount() == 0) villain_range_ = Range::FullCombinatorial();
  if (pot_ == 0) {
    pot_ = 20;
    to_call_ = 5;
  }

  int actual_threads = config_.num_threads > 0 ? config_.num_threads : ParallelFor::GetNumThreads();
  ParallelFor::Init(actual_threads);
  result.num_threads_used = actual_threads;

  node_map_.clear();
  iterations_per_thread_.assign(actual_threads, 0);
  thread_ev_.assign(actual_threads, 0);

  std::atomic<int> global_iter{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < actual_threads; t++) {
    threads.emplace_back([&, t]() { SolveThread(t, global_iter, thread_ev_); });
  }
  for (auto& th : threads) th.join();

  auto end_time = std::chrono::high_resolution_clock::now();
  result.total_time_seconds = std::chrono::duration<double>(end_time - start_time).count();

  // Extract strategies
  for (const auto& [key, node] : node_map_) {
    std::array<double, NUM_MC_ACTIONS> strat;
    node.GetAverageStrategy(strat.data());
    result.strategy_map[key] = strat;
  }

  // Exploitability estimate
  double total_regret = 0;
  int count = 0;
  for (const auto& [k, node] : node_map_) {
    for (int a = 0; a < NUM_MC_ACTIONS; a++) {
      total_regret += std::abs(node.cumulative_regret[a]);
      count++;
    }
  }
  result.exploitability_mbb = count > 0 ? (total_regret / count) * 1000 : 0;
  result.total_iterations = config_.iterations;
  result.iterations_per_thread = iterations_per_thread_;
  return result;
}

double ParallelCCFRSolver::BenchmarkSpeedup() {
  int orig_iters = config_.iterations;
  config_.iterations = std::min(500, orig_iters);

  // Serial baseline
  auto t0 = std::chrono::high_resolution_clock::now();
  ParallelFor::Init(1);
  auto r1 = Solve();
  auto t1 = std::chrono::high_resolution_clock::now();
  double serial_time = std::chrono::duration<double>(t1 - t0).count();

  // Parallel
  auto t2 = std::chrono::high_resolution_clock::now();
  ParallelFor::Init();
  auto r2 = Solve();
  auto t3 = std::chrono::high_resolution_clock::now();
  double parallel_time = std::chrono::duration<double>(t3 - t2).count();

  config_.iterations = orig_iters;
  double speedup = serial_time / std::max(parallel_time, 0.001);

  std::cout << "=== Speedup Benchmark ===\n";
  std::cout << "Serial:  " << serial_time << "s\n";
  std::cout << "Parallel: " << parallel_time << "s\n";
  std::cout << "Speedup: " << speedup << "x\n";
  return speedup;
}

std::array<double, NUM_MC_ACTIONS> ParallelCCFRSolver::GetStrategy(const MCInfoSetKey& key) const {
  std::lock_guard<std::mutex> lock(node_map_mutex_);
  auto it = node_map_.find(key);
  if (it == node_map_.end()) {
    std::array<double, NUM_MC_ACTIONS> uniform;
    uniform.fill(1.0 / NUM_MC_ACTIONS);
    return uniform;
  }
  std::array<double, NUM_MC_ACTIONS> out;
  it->second.GetAverageStrategy(out.data());
  return out;
}

}  // namespace phase10
}  // namespace poker_engine
