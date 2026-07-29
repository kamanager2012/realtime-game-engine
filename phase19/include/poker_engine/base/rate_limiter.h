#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace poker_engine::base {

// ==================== Token Bucket Rate Limiter ====================

class RateLimiter {
 public:
  struct Config {
    double capacity;
    double refill_rate;
    double refill_amount;

    static Config PerSecond(double rate) { return {rate, rate, 1.0}; }

    static Config PerMinute(double rate) { return {rate, rate / 60.0, 1.0}; }
  };

  explicit RateLimiter(const Config& config) : config_(config), tokens_(config.capacity) {}

  bool TryConsume(int count = 1) {
    std::lock_guard<std::mutex> lock(mutex_);
    Refill();

    if (tokens_ >= count) {
      tokens_ -= count;
      return true;
    }
    return false;
  }

  void Wait(int count = 1) {
    while (!TryConsume(count)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  double WaitTime(int count = 1) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tokens_ >= count) return 0.0;
    return (count - tokens_) / config_.refill_rate;
  }

  double Available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tokens_;
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    tokens_ = config_.capacity;
    last_refill_ = Clock::now();
  }

 private:
  using Clock = std::chrono::steady_clock;

  void Refill() {
    auto now = Clock::now();
    double elapsed = std::chrono::duration<double>(now - last_refill_).count();

    double new_tokens = elapsed * config_.refill_rate;
    tokens_ = std::min(config_.capacity, tokens_ + new_tokens);
    last_refill_ = now;
  }

  Config config_;
  double tokens_;
  Clock::time_point last_refill_ = Clock::now();
  mutable std::mutex mutex_;
};

// ==================== Per-Key Rate Limiter ====================

class PerKeyRateLimiter {
 public:
  PerKeyRateLimiter(const RateLimiter::Config& config, size_t max_keys = 10000)
      : config_(config), max_keys_(max_keys) {}

  bool TryConsume(const std::string& key, int count = 1) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = limiters_.find(key);
    if (it == limiters_.end()) {
      if (limiters_.size() >= max_keys_) {
        CleanupOld();
      }
      auto [new_it, _] = limiters_.emplace(key, std::make_unique<RateLimiter>(config_));
      it = new_it;
    }

    return it->second->TryConsume(count);
  }

 private:
  void CleanupOld() {
    size_t target = limiters_.size() / 2;
    auto it = limiters_.begin();
    for (size_t i = 0; i < target && it != limiters_.end(); ++i) {
      it = limiters_.erase(it);
    }
  }

  RateLimiter::Config config_;
  size_t max_keys_;
  std::unordered_map<std::string, std::unique_ptr<RateLimiter>> limiters_;
  mutable std::mutex mutex_;
};

// ==================== 全局限速器 ====================

class RateLimiters {
 public:
  static PerKeyRateLimiter& WSConnectionPerIP() {
    static PerKeyRateLimiter limiter(RateLimiter::Config{10, 0.1, 1.0});
    return limiter;
  }

  static PerKeyRateLimiter& LoginPerIP() {
    static PerKeyRateLimiter limiter(RateLimiter::Config{5, 0.08, 1.0});
    return limiter;
  }

  static PerKeyRateLimiter& RegisterPerIP() {
    // 比登录更宽松，但能挡住批量机器人注册；约 3 次/分钟
    static PerKeyRateLimiter limiter(RateLimiter::Config{15, 0.05, 1.0});
    return limiter;
  }

  static PerKeyRateLimiter& ActionPerPlayer() {
    static PerKeyRateLimiter limiter(RateLimiter::Config::PerSecond(2.0));
    return limiter;
  }

  static PerKeyRateLimiter& ChatPerPlayer() {
    // 允许少量突发（3 条），之后约 1 条/秒；挡住刷屏/广告轰炸
    static PerKeyRateLimiter limiter(RateLimiter::Config{3.0, 1.0, 1.0});
    return limiter;
  }
};

}  // namespace poker_engine::base
