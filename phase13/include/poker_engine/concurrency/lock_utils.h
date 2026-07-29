#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>

namespace poker_engine::concurrency {

template <typename T>
class ReadWriteLock {
 public:
  class ReadGuard {
   public:
    explicit ReadGuard(ReadWriteLock& rw) : rw_(rw) { rw_.mtx_.lock_shared(); }
    ~ReadGuard() { rw_.mtx_.unlock_shared(); }
    const T& Get() const { return rw_.data_; }

   private:
    ReadWriteLock& rw_;
  };

  class WriteGuard {
   public:
    explicit WriteGuard(ReadWriteLock& rw) : rw_(rw) { rw_.mtx_.lock(); }
    ~WriteGuard() { rw_.mtx_.unlock(); }
    T& Get() { return rw_.data_; }
    void Set(const T& value) { rw_.data_ = value; }
    void Set(T&& value) { rw_.data_ = std::move(value); }

   private:
    ReadWriteLock& rw_;
  };

  ReadGuard Read() { return ReadGuard(*this); }
  WriteGuard Write() { return WriteGuard(*this); }

 private:
  mutable std::shared_mutex mtx_;
  T data_;
};

class AtomicFlag {
 public:
  AtomicFlag() : flag_(false) {}
  bool TrySet() {
    bool expected = false;
    return flag_.compare_exchange_strong(expected, true);
  }
  void Clear() { flag_.store(false); }
  bool IsSet() const { return flag_.load(); }

 private:
  std::atomic<bool> flag_;
};

template <typename Mutex>
class ScopedLock {
 public:
  explicit ScopedLock(Mutex& m) : mtx_(m) { mtx_.lock(); }
  ~ScopedLock() { mtx_.unlock(); }
  ScopedLock(const ScopedLock&) = delete;
  ScopedLock& operator=(const ScopedLock&) = delete;

 private:
  Mutex& mtx_;
};

template <typename T, typename Mutex>
class DoubleCheckedLocking {
 public:
  template <typename Creator>
  T& GetOrCreate(Creator&& creator) {
    if (!ptr_) {
      std::lock_guard<Mutex> lock(mtx_);
      if (!ptr_) {
        ptr_ = creator();
      }
    }
    return *ptr_;
  }
  void Reset() {
    std::lock_guard<Mutex> lock(mtx_);
    ptr_.reset();
  }

 private:
  std::unique_ptr<T> ptr_;
  Mutex mtx_;
};

}  // namespace poker_engine::concurrency
