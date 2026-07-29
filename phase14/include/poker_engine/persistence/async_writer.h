#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "poker_engine/base/logging.h"

namespace poker_engine::persistence {

class AsyncWriter {
 public:
  using WriteTask = std::function<void()>;

  AsyncWriter() = default;
  ~AsyncWriter() { Stop(); }

  void Start() {
    running_ = true;
    flush_pending_ = false;
    worker_ = std::thread([this]() { WorkerLoop(); });
    LOG_INFO("AsyncWriter started");
  }

  void Stop() {
    if (!running_) return;
    running_ = false;
    flush_pending_.store(true);
    cv_.notify_one();
    if (worker_.joinable()) {
      worker_.join();
    }
    FlushNow();
    LOG_INFO("AsyncWriter stopped");
  }

  void Enqueue(WriteTask task) {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      task_queue_.push(std::move(task));
    }
    cv_.notify_one();
  }

  void FlushNow() {
    std::queue<WriteTask> local_queue;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      local_queue.swap(task_queue_);
    }
    while (!local_queue.empty()) {
      try {
        local_queue.front()();
      } catch (const std::exception& e) {
        LOG_ERROR("AsyncWriter task failed: {}", e.what());
      }
      local_queue.pop();
    }
  }

  size_t PendingCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_.size();
  }

  void SetBatchThreshold(size_t n) { batch_threshold_ = n; }
  void SetMaxDelayMs(int ms) { max_delay_ms_ = ms; }

 private:
  void WorkerLoop() {
    while (running_) {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(max_delay_ms_),
                   [this]() { return !task_queue_.empty() || !running_; });

      if (!running_ && task_queue_.empty()) break;

      if (task_queue_.size() >= batch_threshold_ || (!running_ && !task_queue_.empty()) ||
          flush_pending_.load()) {
        std::queue<WriteTask> local_queue;
        local_queue.swap(task_queue_);
        flush_pending_.store(false);
        lock.unlock();

        size_t processed = 0;
        while (!local_queue.empty()) {
          try {
            local_queue.front()();
          } catch (const std::exception& e) {
            LOG_ERROR("DB write task failed: {}", e.what());
          }
          local_queue.pop();
          processed++;
        }
        if (processed > 0) {
          LOG_DEBUG("AsyncWriter flushed {} tasks", processed);
        }
      }
    }
    FlushNow();
  }

  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<bool> flush_pending_{false};

  std::queue<WriteTask> task_queue_;
  mutable std::mutex queue_mutex_;
  std::condition_variable cv_;

  size_t batch_threshold_ = 64;
  int max_delay_ms_ = 100;
};

}  // namespace poker_engine::persistence
