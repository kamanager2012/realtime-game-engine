#include "poker_engine/phase10/parallel_utils.h"

#include <omp.h>

namespace poker_engine {
namespace phase10 {

int ParallelFor::num_threads_ = -1;

void ParallelFor::Init(int num_threads) {
  if (num_threads > 0) {
    num_threads_ = num_threads;
#ifdef _OPENMP
    omp_set_num_threads(num_threads);
#endif
  } else {
#ifdef _OPENMP
    num_threads_ = omp_get_max_threads();
#else
    num_threads_ = std::max(1, (int)std::thread::hardware_concurrency());
#endif
  }
}

int ParallelFor::GetNumThreads() {
  if (num_threads_ <= 0) Init(-1);
  return num_threads_;
}

void ParallelFor::Range(int n, const std::function<void(int)>& func) { Range(0, n, func); }

void ParallelFor::Range(int start, int end, const std::function<void(int)>& func) {
  if (end <= start) return;
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 8)
  for (int i = start; i < end; i++) func(i);
#else
  int nt = GetNumThreads();
  std::vector<std::thread> threads;
  int chunk = (end - start + nt - 1) / nt;
  for (int t = 0; t < nt; t++) {
    int ts = start + t * chunk;
    int te = std::min(ts + chunk, end);
    if (ts >= te) break;
    threads.emplace_back([&, ts, te]() {
      for (int i = ts; i < te; i++) func(i);
    });
  }
  for (auto& th : threads) th.join();
#endif
}

// ===================== ThreadPool =====================
ThreadPool::ThreadPool(int num_threads) {
  if (num_threads <= 0) num_threads = std::max(1, (int)std::thread::hardware_concurrency());
  for (int i = 0; i < num_threads; i++) {
    workers_.emplace_back([this]() {
      while (true) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(queue_mutex_);
          condition_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
          if (stop_ && tasks_.empty()) return;
          task = std::move(tasks_.front());
          tasks_.pop();
        }
        task();
        active_tasks_--;
        completed_.notify_one();
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    stop_ = true;
  }
  condition_.notify_all();
  for (auto& w : workers_)
    if (w.joinable()) w.join();
}

void ThreadPool::WaitAll() {
  std::unique_lock<std::mutex> lock(queue_mutex_);
  completed_.wait(lock, [this]() { return active_tasks_ == 0 && tasks_.empty(); });
}

void ThreadPool::ParallelFor(int n, const std::function<void(int)>& func) {
  for (int i = 0; i < n; i++) Submit([&, i]() { func(i); });
  WaitAll();
}

// ===================== ParallelRNG =====================
thread_local int ParallelRNG::thread_id_ = -1;
int ParallelRNG::next_thread_id_ = 0;
std::mutex ParallelRNG::rng_mutex_;

ParallelRNG::ParallelRNG(uint64_t base_seed) : base_seed_(base_seed) {
#ifdef _OPENMP
  int nt = omp_get_max_threads();
#else
  int nt = std::max(1, (int)std::thread::hardware_concurrency());
#endif
  per_thread_rng_.resize(nt);
  for (int i = 0; i < nt; i++)
    per_thread_rng_[i] = std::make_unique<std::mt19937>(base_seed + i * 1234567ULL);
}

std::mt19937& ParallelRNG::GetRNG() {
  if (thread_id_ == -1) {
    std::lock_guard<std::mutex> lock(rng_mutex_);
    thread_id_ = next_thread_id_++;
  }
  return *per_thread_rng_[thread_id_ % static_cast<int>(per_thread_rng_.size())];
}

std::vector<double> ParallelRNG::SampleUniform(int n) {
  std::vector<double> result(n);
  ParallelFor::Range(n, [&](int i) { result[i] = std::generate_canonical<double, 10>(GetRNG()); });
  return result;
}

// ===================== Profiler =====================
std::string Profiler::GetReport() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(4);
  oss << "=== Profiler Report ===\n\n";
  oss << std::setw(30) << std::left << "Section" << std::setw(10) << "Calls" << std::setw(12)
      << "Total(s)" << std::setw(12) << "Avg(us)\n";
  oss << std::string(66, '-') << "\n";
  for (const auto& [name, total] : total_times_) {
    int calls = call_counts_.count(name) ? call_counts_.at(name) : 0;
    double avg_us = calls > 0 ? (total / calls) * 1e6 : 0;
    oss << std::setw(30) << std::left << name << std::setw(10) << calls << std::setw(12) << total
        << std::setw(12) << int(avg_us) << "\n";
  }
  return oss.str();
}

}  // namespace phase10
}  // namespace poker_engine
