#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

namespace poker_engine {
namespace phase10 {

// ========== Parallel For ==========
class ParallelFor {
 public:
  static void Init(int num_threads = -1);
  static int GetNumThreads();
  static void Range(int n, const std::function<void(int)>& func);
  static void Range(int start, int end, const std::function<void(int)>& func);

 private:
  static int num_threads_;
};

// ========== Thread Pool ==========
class ThreadPool {
 public:
  explicit ThreadPool(int num_threads = -1);
  ~ThreadPool();
  template <typename F>
  auto Submit(F&& f) -> std::future<std::result_of_t<F()>>;
  void WaitAll();
  int NumThreads() const { return static_cast<int>(workers_.size()); }
  void ParallelFor(int n, const std::function<void(int)>& func);

 private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex queue_mutex_;
  std::condition_variable condition_;
  std::condition_variable completed_;
  std::atomic<bool> stop_{false};
  std::atomic<int> active_tasks_{0};
};

// ========== Atomic Accumulator ==========
template <typename T>
class AtomicAccumulator {
 public:
  void Add(T v) {
    T cur = value_.load(std::memory_order_relaxed);
    while (!value_.compare_exchange_weak(cur, cur + v, std::memory_order_release,
                                         std::memory_order_relaxed));
  }
  T Get() const { return value_.load(std::memory_order_acquire); }
  void Reset() { value_.store(0, std::memory_order_release); }

 private:
  std::atomic<T> value_{0};
};

// ========== Parallel RNG ==========
class ParallelRNG {
 public:
  explicit ParallelRNG(uint64_t base_seed = 42);
  std::mt19937& GetRNG();
  std::vector<double> SampleUniform(int n);

 private:
  uint64_t base_seed_;
  std::vector<std::unique_ptr<std::mt19937>> per_thread_rng_;
  static thread_local int thread_id_;
  static int next_thread_id_;
  static std::mutex rng_mutex_;
};

// ========== Stopwatch ==========
class Stopwatch {
 public:
  Stopwatch() { Start(); }
  void Start() {
    start_ = std::chrono::high_resolution_clock::now();
    running_ = true;
  }
  void Stop() {
    end_ = std::chrono::high_resolution_clock::now();
    running_ = false;
    accumulated_ += std::chrono::duration<double>(end_ - start_).count();
  }
  double ElapsedSeconds() const {
    if (running_)
      return accumulated_ +
             std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_)
                 .count();
    return accumulated_;
  }
  void Reset() {
    accumulated_ = 0;
    running_ = false;
  }

 private:
  std::chrono::high_resolution_clock::time_point start_, end_;
  bool running_ = false;
  double accumulated_ = 0;
};

// ========== Profiler ==========
class Profiler {
 public:
  static Profiler& Instance() {
    static Profiler inst;
    return inst;
  }
  void StartSection(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    sections_[name].Start();
    call_counts_[name]++;
  }
  void EndSection(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sections_.count(name)) {
      sections_[name].Stop();
      total_times_[name] += sections_[name].ElapsedSeconds();
    }
  }
  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    sections_.clear();
    call_counts_.clear();
    total_times_.clear();
  }
  std::string GetReport() const;
  std::map<std::string, double> GetTimings() const { return total_times_; }

 private:
  std::map<std::string, Stopwatch> sections_;
  std::map<std::string, int> call_counts_;
  std::map<std::string, double> total_times_;
  mutable std::mutex mutex_;
};

// ========== Template Implementations ==========
template <typename F>
auto ThreadPool::Submit(F&& f) -> std::future<std::result_of_t<F()>> {
  using ReturnType = std::result_of_t<F()>;
  auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::forward<F>(f));
  std::future<ReturnType> result = task->get_future();
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (stop_) throw std::runtime_error("ThreadPool stopped");
    tasks_.push([task]() { (*task)(); });
    active_tasks_++;
  }
  condition_.notify_one();
  return result;
}

}  // namespace phase10
}  // namespace poker_engine
