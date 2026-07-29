#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

namespace poker_engine::concurrency {

template <typename M1, typename M2>
class OrderedLock {
 public:
  OrderedLock(M1& m1, M2& m2) : lock1_(m1, std::defer_lock), lock2_(m2, std::defer_lock) {
    if (reinterpret_cast<uintptr_t>(&m1) < reinterpret_cast<uintptr_t>(&m2)) {
      lock1_.lock();
      lock2_.lock();
    } else {
      lock2_.lock();
      lock1_.lock();
    }
  }
  ~OrderedLock() {
    if (lock1_.owns_lock()) lock1_.unlock();
    if (lock2_.owns_lock()) lock2_.unlock();
  }

 private:
  std::unique_lock<M1> lock1_;
  std::unique_lock<M2> lock2_;
};

template <typename M1, typename M2>
auto LockOrdered(M1& m1, M2& m2) {
  return OrderedLock<M1, M2>(m1, m2);
}

template <typename Mutex>
class TimedLock {
 public:
  TimedLock(Mutex& m, int timeout_ms) : lock_(m, std::defer_lock) {
    acquired_ = lock_.try_lock_for(std::chrono::milliseconds(timeout_ms));
  }
  bool Acquired() const { return acquired_; }
  void Unlock() {
    if (acquired_) lock_.unlock();
  }
  ~TimedLock() {
    if (acquired_) lock_.unlock();
  }

 private:
  std::unique_lock<Mutex> lock_;
  bool acquired_;
};

template <typename RWMutex>
class WriteLockGuard {
 public:
  explicit WriteLockGuard(RWMutex& mtx) : mtx_(mtx) { mtx_.lock(); }
  ~WriteLockGuard() { mtx_.unlock(); }

 private:
  RWMutex& mtx_;
};

template <typename RWMutex>
class ReadLockGuard {
 public:
  explicit ReadLockGuard(RWMutex& mtx) : mtx_(mtx) { mtx_.lock_shared(); }
  ~ReadLockGuard() { mtx_.unlock_shared(); }

 private:
  RWMutex& mtx_;
};

class SpinLock {
 public:
  void Lock() {
    while (flag_.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(_M_X64)
      _mm_pause();
#endif
    }
  }
  void Unlock() { flag_.clear(std::memory_order_release); }
  bool TryLock() { return !flag_.test_and_set(std::memory_order_acquire); }

 private:
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

class SpinLockGuard {
 public:
  explicit SpinLockGuard(SpinLock& lock) : lock_(lock) { lock_.Lock(); }
  ~SpinLockGuard() { lock_.Unlock(); }

 private:
  SpinLock& lock_;
};

class LockStatistics {
 public:
  void RecordAcquire(uint64_t microseconds) {
    total_acquires_++;
    total_wait_us_ += microseconds;
    if (microseconds > max_wait_us_) max_wait_us_ = microseconds;
  }
  void RecordContention() { contention_count_++; }
  void Print() const {
    if (total_acquires_ == 0) return;
    double avg_us = static_cast<double>(total_wait_us_) / total_acquires_;
    // LOG_INFO would require including logging.h — use printf for header-only
  }

 private:
  uint64_t total_acquires_ = 0;
  uint64_t total_wait_us_ = 0;
  uint64_t max_wait_us_ = 0;
  uint64_t contention_count_ = 0;
};

}  // namespace poker_engine::concurrency
