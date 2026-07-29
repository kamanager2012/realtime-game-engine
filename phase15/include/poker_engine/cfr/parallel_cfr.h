#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

#include "cfr_abstraction.h"
#include "cfr_engine.h"
#include "cfr_node.h"
#include "cfr_training.h"
#include "types.h"

namespace poker_engine::cfr {

struct TrainingSample {
  InfosetKey infoset;
  double reach_prob = 0.0;
  double counterfactual_value = 0.0;
  int player = 0;
  int iteration = 0;
};

class Sampler {
 public:
  explicit Sampler(int batch_size = 1024, uint64_t seed = 42);

  std::vector<TrainingSample> SampleBatch(const std::unordered_map<uint64_t, CFRNode>& nodes,
                                          int player);

  std::vector<TrainingSample> SampleStratified(const std::unordered_map<uint64_t, CFRNode>& nodes,
                                               int num_strata);

  void SetBatchSize(int size) { batch_size_ = size; }
  int BatchSize() const { return batch_size_; }

  void SetImportanceThreshold(double threshold) { importance_threshold_ = threshold; }

  std::vector<InfosetKey> GetHighRegretInfosets(const std::unordered_map<uint64_t, CFRNode>& nodes,
                                                int top_k) const;

 private:
  int batch_size_;
  double importance_threshold_ = 0.01;
  std::mt19937 rng_;
};

struct ParallelCFROptions {
  CFROptions base_options;

  int num_threads = 4;
  int batch_size = 1024;
  int sync_interval = 10;
  bool use_batch_sampling = true;
  bool use_thread_local_rng = true;
  double regret_sync_threshold = 0.0;
  int pool_initial_capacity = 1 << 20;
};

class CFRNodePool {
 public:
  explicit CFRNodePool(int initial_capacity = 1 << 20);

  CFRNode* GetOrCreate(const InfosetKey& key);
  CFRNode* Get(const InfosetKey& key);
  const CFRNode* Get(const InfosetKey& key) const;

  bool Contains(const InfosetKey& key) const;
  size_t Size() const;

  void MergeRegretUpdates(const std::vector<std::pair<InfosetKey, double[kMaxActions]>>& updates);

  void Compact();
  void Clear();

  std::unordered_map<uint64_t, CFRNode> Snapshot() const;

 private:
  std::unordered_map<uint64_t, CFRNode> nodes_;
  mutable std::mutex mutex_;
  int capacity_;
};

class ParallelCFRTrainer {
 public:
  explicit ParallelCFRTrainer(const ParallelCFROptions& options = ParallelCFROptions());

  void Initialize();
  void Train(int num_iterations);
  void Stop();

  double ComputeExploitability();

  size_t NodeCount() const;
  const CFRNodePool& Pool() const { return pool_; }

  std::vector<std::pair<Action, double>> GetStrategy(const InfosetKey& key);

  void SetCallback(TrainingCallback cb) { callback_ = std::move(cb); }

  bool SaveModel(const std::string& filepath);
  bool LoadModel(const std::string& filepath);

  struct TrainingStats {
    int iterations_completed = 0;
    double exploitability = 0.0;
    size_t total_nodes = 0;
    double elapsed_ms = 0.0;
    int threads_used = 0;
    double samples_per_second = 0.0;
  };
  TrainingStats GetStats() const { return stats_; }

 private:
  void WorkerThread(int thread_id, int start_iter, int end_iter);
  void SyncRegrets();
  void SampleAndTrain(int thread_id, int iterations);

  ParallelCFROptions options_;
  CFRNodePool pool_;
  Sampler sampler_;
  HandAbstraction hand_abstraction_;

  std::vector<std::thread> workers_;
  std::atomic<bool> running_{false};
  std::atomic<int> iteration_counter_{0};
  std::mutex sync_mutex_;
  std::condition_variable sync_cv_;
  int threads_at_barrier_ = 0;

  TrainingCallback callback_;
  TrainingStats stats_;

  std::vector<std::vector<std::pair<InfosetKey, double[kMaxActions]>>> pending_updates_;
};

}  // namespace poker_engine::cfr
