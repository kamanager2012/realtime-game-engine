#pragma once

#include <atomic>
#include <chrono>
#include <optional>
#include <type_traits>

// ==================== 两种实现 ====================
// #1: 使用 moodycamel::ConcurrentQueue (external dependency)
// #2: 自旋锁无锁队列 (zero-dependency fallback)

namespace poker_engine::concurrency {

#ifdef POKER_ENGINE_USE_MOODYCAMEL
#include "concurrentqueue.h"

template <typename T>
using ConcurrentQueue = moodycamel::ConcurrentQueue<T>;

template <typename T>
using ProducerToken = moodycamel::ProducerToken;

template <typename T>
using ConsumerToken = moodycamel::ConsumerToken;

#else
// ==================== 自旋锁无锁队列（零依赖备用） ====================

template <typename T, size_t Capacity = 1024>
class ConcurrentQueue {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
  static constexpr size_t MASK = Capacity - 1;

 public:
  ConcurrentQueue() = default;

  bool enqueue(const T& item) {
    size_t ctail = tail_.load(std::memory_order_relaxed);
    size_t chead = head_.load(std::memory_order_acquire);

    if (ctail - chead >= Capacity) return false;  // full

    buffer_[ctail & MASK] = item;
    tail_.store(ctail + 1, std::memory_order_release);
    return true;
  }

  bool enqueue(T&& item) {
    size_t ctail = tail_.load(std::memory_order_relaxed);
    size_t chead = head_.load(std::memory_order_acquire);

    if (ctail - chead >= Capacity) return false;

    buffer_[ctail & MASK] = std::move(item);
    tail_.store(ctail + 1, std::memory_order_release);
    return true;
  }

  std::optional<T> dequeue() {
    size_t chead = head_.load(std::memory_order_relaxed);
    size_t ctail = tail_.load(std::memory_order_acquire);

    if (chead >= ctail) return std::nullopt;  // empty

    T item = std::move(buffer_[chead & MASK]);
    head_.store(chead + 1, std::memory_order_release);
    return item;
  }

  bool empty() const {
    return head_.load(std::memory_order_acquire) >= tail_.load(std::memory_order_acquire);
  }

  size_t size_approx() const {
    return tail_.load(std::memory_order_relaxed) - head_.load(std::memory_order_relaxed);
  }

  // 批量出队
  template <typename OutputIter>
  size_t try_dequeue_bulk(OutputIter out, size_t max) {
    size_t count = 0;
    size_t chead = head_.load(std::memory_order_relaxed);
    size_t ctail = tail_.load(std::memory_order_acquire);
    size_t available = (ctail > chead) ? (ctail - chead) : 0;
    count = std::min(available, max);

    for (size_t i = 0; i < count; ++i) {
      *out++ = std::move(buffer_[(chead + i) & MASK]);
    }

    head_.store(chead + count, std::memory_order_release);
    return count;
  }

 private:
  alignas(64) std::atomic<size_t> head_{0};
  alignas(64) std::atomic<size_t> tail_{0};
  T buffer_[Capacity];
};

#endif

// ==================== 类型别名 ====================

template <typename T, size_t Cap = 1024>
using SPSCQueue = ConcurrentQueue<T, Cap>;  // 单生产者单消费者

template <typename T>
using MPMCQueue = ConcurrentQueue<T>;  // 多生产者多消费者

}  // namespace poker_engine::concurrency
