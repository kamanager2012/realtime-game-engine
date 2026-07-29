#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "poker_engine/base/logging.h"

namespace poker_engine::metrics {

// ==================== 指标类型 ====================

enum class MetricType : uint8_t {
  Counter = 0,    // 单调递增
  Gauge = 1,      // 可增可减
  Histogram = 2,  // 分布统计
};

// ==================== 计数器 ====================

class Counter {
 public:
  Counter() = default;
  explicit Counter(int64_t initial = 0) : value_(initial) {}

  void Increment(int64_t delta = 1) { value_.fetch_add(delta, std::memory_order_relaxed); }
  void Decrement(int64_t delta = 1) { value_.fetch_sub(delta, std::memory_order_relaxed); }
  int64_t Value() const { return value_.load(std::memory_order_relaxed); }

 private:
  std::atomic<int64_t> value_{0};
};

// ==================== 仪表盘 ====================

class Gauge {
 public:
  Gauge() = default;
  explicit Gauge(double initial = 0.0) : value_(initial) {}

  void Set(double val) { value_.store(val, std::memory_order_relaxed); }
  void Add(double val) { value_.fetch_add(val, std::memory_order_relaxed); }
  void Subtract(double val) { value_.fetch_sub(val, std::memory_order_relaxed); }
  double Value() const { return value_.load(std::memory_order_relaxed); }

 private:
  std::atomic<double> value_{0.0};
};

// ==================== 直方图 ====================

class Histogram {
 public:
  explicit Histogram(const std::vector<double>& buckets = {0.001, 0.005, 0.01, 0.025, 0.05, 0.1,
                                                           0.25, 0.5, 1.0, 2.5, 5.0, 10.0})
      : buckets_(buckets), counts_(buckets.size() + 1, 0) {}

  void Observe(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    sum_.fetch_add(value, std::memory_order_relaxed);
    count_.fetch_add(1, std::memory_order_relaxed);

    size_t i = 0;
    for (; i < buckets_.size(); ++i) {
      if (value <= buckets_[i]) break;
    }
    counts_[i].fetch_add(1, std::memory_order_relaxed);

    // 更新最大值/最小值
    double cur = max_.load(std::memory_order_relaxed);
    while (value > cur && !max_.compare_exchange_weak(cur, value)) {
    }

    cur = min_.load(std::memory_order_relaxed);
    while (value < cur && !min_.compare_exchange_weak(cur, value)) {
    }
  }

  // 时间计时器
  class Timer {
   public:
    explicit Timer(Histogram* h) : hist_(h), start_(std::chrono::steady_clock::now()) {}
    ~Timer() {
      auto elapsed =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
      hist_->Observe(elapsed);
    }

   private:
    Histogram* hist_;
    std::chrono::steady_clock::time_point start_;
  };

  std::string Serialize() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    oss << "count " << count_.load() << "\n";
    oss << "sum " << sum_.load() << "\n";

    int64_t cumulative = 0;
    for (size_t i = 0; i < buckets_.size(); ++i) {
      cumulative += counts_[i].load();
      oss << "bucket_" << buckets_[i] << " " << cumulative << "\n";
    }
    cumulative += counts_.back().load();
    oss << "bucket_inf " << cumulative << "\n";

    oss << "min " << min_.load() << "\n";
    oss << "max " << max_.load() << "\n";

    return oss.str();
  }

 private:
  std::vector<double> buckets_;
  std::vector<std::atomic<int64_t>> counts_;
  std::atomic<double> sum_{0.0};
  std::atomic<int64_t> count_{0};
  std::atomic<double> min_{std::numeric_limits<double>::max()};
  std::atomic<double> max_{0.0};
  mutable std::mutex mutex_;
};

// ==================== 指标注册表 ====================

class MetricRegistry {
 public:
  static MetricRegistry& Instance() {
    static MetricRegistry instance;
    return instance;
  }

  Counter& GetCounter(const std::string& name, const std::string& help = "") {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = counters_.try_emplace(name);
    if (inserted) it->second = std::make_unique<Counter>();
    return *it->second;
  }

  Gauge& GetGauge(const std::string& name, const std::string& help = "") {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = gauges_.try_emplace(name);
    if (inserted) it->second = std::make_unique<Gauge>();
    return *it->second;
  }

  Histogram& GetHistogram(const std::string& name, const std::string& help = "",
                          const std::vector<double>& buckets = {}) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = histograms_.try_emplace(name);
    if (inserted) it->second = std::make_unique<Histogram>(buckets);
    return *it->second;
  }

  // 导出为 Prometheus 格式
  std::string ExportPrometheus() const {
    std::ostringstream oss;

    oss << "# HELP poker_messages_total Total messages processed\n";
    oss << "# TYPE poker_messages_total counter\n";
    oss << "poker_messages_total " << GetCounter("messages_total").Value() << "\n\n";

    oss << "# HELP poker_ws_connections Current WebSocket connections\n";
    oss << "# TYPE poker_ws_connections gauge\n";
    oss << "poker_ws_connections " << GetGauge("ws_connections").Value() << "\n\n";

    oss << "# HELP poker_message_latency_seconds Message processing latency\n";
    oss << "# TYPE poker_message_latency_seconds histogram\n";
    oss << GetHistogram("message_latency").Serialize();
    oss << "\n";

    oss << "# HELP poker_active_connections Active WS connections counter\n";
    oss << "# TYPE poker_active_connections gauge\n";
    oss << "poker_active_connections " << GetGauge("active_connections").Value() << "\n";

    oss << "# HELP poker_active_tables Number of active tables\n";
    oss << "# TYPE poker_active_tables gauge\n";
    oss << "poker_active_tables " << GetGauge("active_tables").Value() << "\n";

    oss << "# HELP poker_anticheat_alerts_total Anticheat alerts\n";
    oss << "# TYPE poker_anticheat_alerts_total counter\n";
    oss << "poker_anticheat_alerts_total " << GetCounter("anticheat_alerts").Value() << "\n";

    oss << "# HELP poker_db_query_duration_seconds Database query duration\n";
    oss << "# TYPE poker_db_query_duration_seconds histogram\n";
    oss << "poker_db_query_duration_seconds " << GetHistogram("db_query_duration").Serialize();

    return oss.str();
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.clear();
    gauges_.clear();
    histograms_.clear();
  }

 private:
  MetricRegistry() = default;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::unique_ptr<Counter>> counters_;
  std::unordered_map<std::string, std::unique_ptr<Gauge>> gauges_;
  std::unordered_map<std::string, std::unique_ptr<Histogram>> histograms_;
};

// ==================== 便捷宏 ====================

#define METRIC_COUNTER(name) (poker_engine::metrics::MetricRegistry::Instance().GetCounter(name))
#define METRIC_GAUGE(name) (poker_engine::metrics::MetricRegistry::Instance().GetGauge(name))
#define METRIC_HISTOGRAM(name) \
  (poker_engine::metrics::MetricRegistry::Instance().GetHistogram(name))
#define METRIC_TIMER(name) (METRIC_HISTOGRAM(name).Timer())

}  // namespace poker_engine::metrics
