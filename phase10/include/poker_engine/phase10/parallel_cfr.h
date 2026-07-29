#pragma once
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "poker_engine/evaluator/card.h"
#include "poker_engine/phase10/parallel_utils.h"
#include "poker_engine/phase8/mc_cfr_deep.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase10 {

using phase8::MCInfoSetKey;
using phase8::MCNode;
using phase8::MCRMode;
using phase8::NUM_MC_ACTIONS;

struct ParallelCFRConfig {
  int iterations = 5000;
  int mc_samples_per_iter = 50;
  int num_threads = -1;
  MCRMode mode = MCRMode::EXTERNAL_SAMPLING;
  double discount_factor = 0.995;
  int max_depth = 12;
  bool verbose = false;
};

struct ParallelCFRResult {
  std::map<MCInfoSetKey, std::array<double, NUM_MC_ACTIONS>> strategy_map;
  double exploitability_mbb = 0;
  int total_iterations = 0;
  double total_time_seconds = 0;
  double speedup_vs_serial = 0;
  int num_threads_used = 0;
  std::vector<int64_t> iterations_per_thread;

  std::string ToString() const;
  std::string SpeedupReport() const;
};

class ParallelCCFRSolver {
 public:
  explicit ParallelCCFRSolver(const ParallelCFRConfig& config = ParallelCFRConfig());
  void SetHeroRange(const poker_engine::range::Range& r);
  void SetVillainRange(const poker_engine::range::Range& r);
  void SetBoard(const std::vector<poker_engine::Card>& board);
  void SetPot(double pot, double to_call = 0);

  ParallelCFRResult Solve();
  double BenchmarkSpeedup();
  std::array<double, NUM_MC_ACTIONS> GetStrategy(const MCInfoSetKey& key) const;

 private:
  ParallelCFRConfig config_;
  poker_engine::range::Range hero_range_;
  poker_engine::range::Range villain_range_;
  std::vector<poker_engine::Card> board_;
  double pot_ = 0, to_call_ = 0;

  std::map<MCInfoSetKey, MCNode> node_map_;
  mutable std::mutex node_map_mutex_;
  std::vector<int64_t> iterations_per_thread_;
  std::vector<double> thread_ev_;

  void SolveThread(int thread_id, std::atomic<int>& global_iter, std::vector<double>& thread_ev);
  double CFRPartial(int thread_id, int player, double reach_hero, double reach_villain,
                    int street_idx, const std::string& history, int depth);
  double ComputeEquityParallel(int n_samples);
};

}  // namespace phase10
}  // namespace poker_engine
