#include "poker_engine/cfr/parallel_cfr.h"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

#include "poker_engine/base/logging.h"

namespace poker_engine::cfr {

// ==================== Sampler ====================

Sampler::Sampler(int batch_size, uint64_t seed) : batch_size_(batch_size), rng_(seed) {}

std::vector<TrainingSample> Sampler::SampleBatch(const std::unordered_map<uint64_t, CFRNode>& nodes,
                                                 int player) {
  std::vector<TrainingSample> samples;
  samples.reserve(batch_size_);

  if (nodes.empty()) return samples;

  std::vector<uint64_t> keys;
  keys.reserve(nodes.size());
  for (auto& [k, _] : nodes) keys.push_back(k);

  std::uniform_int_distribution<size_t> key_dist(0, keys.size() - 1);
  std::uniform_real_distribution<double> prob_dist(0.0, 1.0);

  for (int i = 0; i < batch_size_; ++i) {
    uint64_t key = keys[key_dist(rng_)];
    const CFRNode& node = nodes.at(key);

    double max_regret = 0.0;
    for (int a = 0; a < CFRNode::kMaxActions; ++a) {
      max_regret = std::max(max_regret, std::abs(node.regret_sum[a]));
    }

    TrainingSample sample;
    sample.infoset.hand_bucket = static_cast<uint16_t>(key & 0xFFFF);
    sample.infoset.street = static_cast<uint8_t>((key >> 16) & 0xFF);
    sample.infoset.pot_size = static_cast<uint16_t>((key >> 24) & 0xFFFF);
    sample.infoset.bet_sequence = static_cast<uint8_t>((key >> 40) & 0xFF);
    sample.infoset.player = static_cast<uint8_t>(player);
    sample.reach_prob = prob_dist(rng_);
    sample.counterfactual_value = 0.0;
    sample.player = player;
    sample.iteration = i;

    if (importance_threshold_ <= 0 || max_regret > importance_threshold_) {
      samples.push_back(sample);
    }
  }

  return samples;
}

std::vector<TrainingSample> Sampler::SampleStratified(
    const std::unordered_map<uint64_t, CFRNode>& nodes, int num_strata) {
  std::vector<TrainingSample> samples;

  if (nodes.empty() || num_strata <= 0) return samples;

  int per_stratum = batch_size_ / num_strata;

  std::vector<std::pair<uint64_t, double>> keyed_regret;
  keyed_regret.reserve(nodes.size());
  for (auto& [k, node] : nodes) {
    double max_r = 0.0;
    for (int a = 0; a < CFRNode::kMaxActions; ++a) {
      max_r = std::max(max_r, std::abs(node.regret_sum[a]));
    }
    keyed_regret.push_back({k, max_r});
  }

  std::sort(keyed_regret.begin(), keyed_regret.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  int stratum_size = static_cast<int>(keyed_regret.size()) / num_strata;
  if (stratum_size < 1) stratum_size = 1;

  std::uniform_real_distribution<double> prob_dist(0.0, 1.0);

  for (int s = 0; s < num_strata; ++s) {
    int start = s * stratum_size;
    int end = std::min(start + stratum_size, static_cast<int>(keyed_regret.size()));
    if (start >= static_cast<int>(keyed_regret.size())) break;

    std::uniform_int_distribution<int> idx_dist(start, end - 1);

    for (int i = 0; i < per_stratum; ++i) {
      int idx = idx_dist(rng_);
      uint64_t key = keyed_regret[idx].first;

      TrainingSample sample;
      sample.infoset.hand_bucket = static_cast<uint16_t>(key & 0xFFFF);
      sample.infoset.street = static_cast<uint8_t>((key >> 16) & 0xFF);
      sample.infoset.pot_size = static_cast<uint16_t>((key >> 24) & 0xFFFF);
      sample.infoset.bet_sequence = static_cast<uint8_t>((key >> 40) & 0xFF);
      sample.reach_prob = prob_dist(rng_);
      sample.counterfactual_value = 0.0;
      sample.iteration = i;
      samples.push_back(sample);
    }
  }

  return samples;
}

std::vector<InfosetKey> Sampler::GetHighRegretInfosets(
    const std::unordered_map<uint64_t, CFRNode>& nodes, int top_k) const {
  std::vector<std::pair<uint64_t, double>> regret_list;
  regret_list.reserve(nodes.size());

  for (auto& [k, node] : nodes) {
    double max_r = 0.0;
    for (int a = 0; a < CFRNode::kMaxActions; ++a) {
      max_r = std::max(max_r, std::abs(node.regret_sum[a]));
    }
    regret_list.push_back({k, max_r});
  }

  std::partial_sort(regret_list.begin(),
                    regret_list.begin() + std::min(top_k, static_cast<int>(regret_list.size())),
                    regret_list.end(),
                    [](const auto& a, const auto& b) { return a.second > b.second; });

  std::vector<InfosetKey> result;
  int count = std::min(top_k, static_cast<int>(regret_list.size()));
  result.reserve(count);
  for (int i = 0; i < count; ++i) {
    uint64_t key = regret_list[i].first;
    InfosetKey ik;
    ik.hand_bucket = static_cast<uint16_t>(key & 0xFFFF);
    ik.street = static_cast<uint8_t>((key >> 16) & 0xFF);
    ik.pot_size = static_cast<uint16_t>((key >> 24) & 0xFFFF);
    ik.bet_sequence = static_cast<uint8_t>((key >> 40) & 0xFF);
    result.push_back(ik);
  }
  return result;
}

// ==================== CFRNodePool ====================

CFRNodePool::CFRNodePool(int initial_capacity) : capacity_(initial_capacity) {}

CFRNode* CFRNodePool::GetOrCreate(const InfosetKey& key) {
  uint64_t h = key.hash();
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = nodes_.find(h);
  if (it != nodes_.end()) return &it->second;
  auto [new_it, _] = nodes_.emplace(h, CFRNode());
  return &new_it->second;
}

CFRNode* CFRNodePool::Get(const InfosetKey& key) {
  uint64_t h = key.hash();
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = nodes_.find(h);
  return it != nodes_.end() ? &it->second : nullptr;
}

const CFRNode* CFRNodePool::Get(const InfosetKey& key) const {
  uint64_t h = key.hash();
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = nodes_.find(h);
  return it != nodes_.end() ? &it->second : nullptr;
}

bool CFRNodePool::Contains(const InfosetKey& key) const {
  uint64_t h = key.hash();
  std::lock_guard<std::mutex> lock(mutex_);
  return nodes_.count(h) > 0;
}

size_t CFRNodePool::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return nodes_.size();
}

void CFRNodePool::MergeRegretUpdates(
    const std::vector<std::pair<InfosetKey, double[kMaxActions]>>& updates) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [key, regret_delta] : updates) {
    uint64_t h = key.hash();
    auto it = nodes_.find(h);
    if (it != nodes_.end()) {
      for (int a = 0; a < kMaxActions; ++a) {
        it->second.regret_sum[a] += regret_delta[a];
      }
      it->second.times_visited++;
    }
  }
}

void CFRNodePool::Compact() {
  std::lock_guard<std::mutex> lock(mutex_);
  nodes_.rehash(0);
}

void CFRNodePool::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  nodes_.clear();
}

std::unordered_map<uint64_t, CFRNode> CFRNodePool::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return nodes_;
}

// ==================== ParallelCFRTrainer ====================

ParallelCFRTrainer::ParallelCFRTrainer(const ParallelCFROptions& options)
    : options_(options), sampler_(options.batch_size), hand_abstraction_(), stats_{} {
  pending_updates_.resize(options.num_threads);
}

void ParallelCFRTrainer::Initialize() {
  pool_.Clear();
  sampler_ = Sampler(options_.batch_size);
  stats_ = TrainingStats{};
  stats_.threads_used = options_.num_threads;

  evaluator::Evaluator eval;
  hand_abstraction_.Initialize(eval);

  PE_LOG_INFO("ParallelCFRTrainer initialized: {} threads, batch_size={}", options_.num_threads,
              options_.batch_size);
}

void ParallelCFRTrainer::Train(int num_iterations) {
  if (running_.load()) {
    PE_LOG_WARN("Training already in progress");
    return;
  }

  running_.store(true);
  iteration_counter_.store(0);

  auto start = std::chrono::steady_clock::now();

  int iters_per_thread = num_iterations / options_.num_threads;
  int remainder = num_iterations % options_.num_threads;

  workers_.clear();
  workers_.reserve(options_.num_threads);

  for (int t = 0; t < options_.num_threads; ++t) {
    int start_iter = t * iters_per_thread + std::min(t, remainder);
    int end_iter = start_iter + iters_per_thread + (t < remainder ? 1 : 0);
    workers_.emplace_back(&ParallelCFRTrainer::WorkerThread, this, t, start_iter, end_iter);
  }

  for (auto& w : workers_) {
    if (w.joinable()) w.join();
  }

  SyncRegrets();

  auto elapsed = std::chrono::steady_clock::now() - start;
  double ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  stats_.iterations_completed = iteration_counter_.load();
  stats_.total_nodes = pool_.Size();
  stats_.elapsed_ms = ms;
  stats_.exploitability = ComputeExploitability();

  if (ms > 0) {
    stats_.samples_per_second = (stats_.iterations_completed * options_.batch_size * 1000.0) / ms;
  }

  running_.store(false);
  PE_LOG_INFO("ParallelCFR training complete: {} iters, {} nodes, {:.1f}ms, {:.0f} samples/s",
              stats_.iterations_completed, stats_.total_nodes, ms, stats_.samples_per_second);
}

void ParallelCFRTrainer::WorkerThread(int thread_id, int start_iter, int end_iter) {
  thread_local std::mt19937 local_rng(42 + thread_id);

  for (int iter = start_iter; iter < end_iter && running_.load(); ++iter) {
    auto snapshot = pool_.Snapshot();

    auto samples = sampler_.SampleBatch(snapshot, iter % 2);

    for (auto& sample : samples) {
      CFRNode* node = pool_.GetOrCreate(sample.infoset);
      if (!node) continue;

      node->compute_strategy();

      double* strategy = node->current_strategy;
      double node_utility = 0.0;
      double action_utils[kMaxActions] = {0};

      for (int a = 0; a < kMaxActions; ++a) {
        action_utils[a] = (strategy[a] > 0) ? (local_rng() % 100) / 100.0 : 0.0;
        node_utility += strategy[a] * action_utils[a];
      }

      std::pair<InfosetKey, double[kMaxActions]> update;
      update.first = sample.infoset;
      for (int a = 0; a < kMaxActions; ++a) {
        double regret = action_utils[a] - node_utility;
        update.second[a] = regret * sample.reach_prob;
      }

      if (thread_id < static_cast<int>(pending_updates_.size())) {
        pending_updates_[thread_id].push_back(update);
      }
    }

    iteration_counter_.fetch_add(1);

    if (iter > start_iter && (iter - start_iter) % options_.sync_interval == 0) {
      SyncRegrets();
    }
  }
}

void ParallelCFRTrainer::SyncRegrets() {
  for (auto& updates : pending_updates_) {
    if (!updates.empty()) {
      pool_.MergeRegretUpdates(updates);
      updates.clear();
    }
  }
}

void ParallelCFRTrainer::SampleAndTrain(int thread_id, int iterations) {
  WorkerThread(thread_id, 0, iterations);
}

void ParallelCFRTrainer::Stop() {
  running_.store(false);
  for (auto& w : workers_) {
    if (w.joinable()) w.join();
  }
  workers_.clear();
}

double ParallelCFRTrainer::ComputeExploitability() {
  auto snapshot = pool_.Snapshot();
  if (snapshot.empty()) return 0.0;

  double total_regret = 0.0;
  int count = 0;

  for (auto& [key, node] : snapshot) {
    double pos_sum = 0.0;
    for (int a = 0; a < CFRNode::kMaxActions; ++a) {
      if (node.regret_sum[a] > 0) pos_sum += node.regret_sum[a];
    }
    total_regret += pos_sum;
    count++;
  }

  return count > 0 ? total_regret / count : 0.0;
}

size_t ParallelCFRTrainer::NodeCount() const { return pool_.Size(); }

std::vector<std::pair<Action, double>> ParallelCFRTrainer::GetStrategy(const InfosetKey& key) {
  std::vector<std::pair<Action, double>> result;
  const CFRNode* node = pool_.Get(key);
  if (!node) {
    for (int a = 0; a < action_count(); ++a)
      result.emplace_back(static_cast<Action>(a), 1.0 / action_count());
    return result;
  }

  double avg[CFRNode::kMaxActions];
  const_cast<CFRNode*>(node)->get_average_strategy(avg);

  for (int a = 0; a < action_count(); ++a) {
    if (avg[a] > 0.001) result.emplace_back(static_cast<Action>(a), avg[a]);
  }
  return result;
}

bool ParallelCFRTrainer::SaveModel(const std::string& filepath) {
  auto snapshot = pool_.Snapshot();
  double exploit = ComputeExploitability();
  return CFRModelIO::Save(filepath, snapshot, exploit);
}

bool ParallelCFRTrainer::LoadModel(const std::string& filepath) {
  std::unordered_map<uint64_t, CFRNode> nodes;
  if (!CFRModelIO::Load(filepath, nodes)) return false;

  pool_.Clear();
  for (auto& [key, node] : nodes) {
    InfosetKey ik;
    ik.hand_bucket = static_cast<uint16_t>(key & 0xFFFF);
    ik.street = static_cast<uint8_t>((key >> 16) & 0xFF);
    ik.pot_size = static_cast<uint16_t>((key >> 24) & 0xFFFF);
    ik.bet_sequence = static_cast<uint8_t>((key >> 40) & 0xFF);
    CFRNode* n = pool_.GetOrCreate(ik);
    if (n) *n = node;
  }

  PE_LOG_INFO("Loaded {} nodes into ParallelCFR pool", nodes.size());
  return true;
}

}  // namespace poker_engine::cfr
