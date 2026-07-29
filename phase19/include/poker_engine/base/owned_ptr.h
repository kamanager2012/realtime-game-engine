#pragma once

#include <memory>
#include <utility>

namespace poker_engine::base {

// ==================== 独占所有权指针（明确所有权语义） ====================

template <typename T>
using Owned = std::unique_ptr<T>;

template <typename T, typename... Args>
Owned<T> MakeOwned(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}

// ==================== 共享所有权指针（引用计数） ====================

template <typename T>
using Shared = std::shared_ptr<T>;

template <typename T, typename... Args>
Shared<T> MakeShared(Args&&... args) {
  return std::make_shared<T>(std::forward<Args>(args)...);
}

// ==================== 弱引用（不延长生命周期） ====================

template <typename T>
using Weak = std::weak_ptr<T>;

// ==================== 作用域退出（RAII 清理） ====================

template <typename F>
class ScopeGuard {
 public:
  explicit ScopeGuard(F&& f) : cleanup_(std::forward<F>(f)), active_(true) {}
  ~ScopeGuard() {
    if (active_) cleanup_();
  }

  void Dismiss() { active_ = false; }

  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
  ScopeGuard(ScopeGuard&& other) : cleanup_(std::move(other.cleanup_)), active_(other.active_) {
    other.active_ = false;
  }

 private:
  F cleanup_;
  bool active_;
};

template <typename F>
ScopeGuard<F> OnScopeExit(F&& f) {
  return ScopeGuard<F>(std::forward<F>(f));
}

// ==================== 非空指针包装 ====================

template <typename T>
class NonNull {
 public:
  explicit NonNull(T* ptr) : ptr_(ptr) {
    if (!ptr_) throw std::invalid_argument("NonNull: null pointer");
  }

  T& operator*() const { return *ptr_; }
  T* operator->() const { return ptr_; }

  T* Get() const { return ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }

 private:
  T* ptr_;
};

}  // namespace poker_engine::base
