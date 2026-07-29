#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace poker_engine::observability {

// Forward declaration
class Tracer;

// ==================== OpenTelemetry 兼容 Tracer ====================
// 提供分布式链路追踪能力

struct SpanContext {
  std::string trace_id;  // 128-bit trace ID (hex)
  std::string span_id;   // 64-bit span ID (hex)
  std::string parent_span_id;

  bool IsValid() const { return !trace_id.empty(); }
};

struct Span {
  SpanContext context;
  std::string name;
  std::string operation;

  std::chrono::steady_clock::time_point start_time;
  std::chrono::steady_clock::time_point end_time;

  std::unordered_map<std::string, std::string> tags;
  std::unordered_map<std::string, std::string> baggage;
  std::vector<std::string> events;  // Span 内的事件

  bool is_recording = true;

  void SetTag(const std::string& key, const std::string& value) { tags[key] = value; }

  void SetTag(const std::string& key, int64_t value) { tags[key] = std::to_string(value); }

  void SetTag(const std::string& key, double value) { tags[key] = std::to_string(value); }

  void AddEvent(const std::string& event_name) { events.push_back(event_name); }

  void End() { end_time = std::chrono::steady_clock::now(); }

  int64_t DurationMicroseconds() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
  }
};

// ==================== Span 构建器 ====================

class SpanBuilder {
 public:
  explicit SpanBuilder(Tracer* tracer, const std::string& name) : tracer_(tracer), span_() {
    span_.name = name;
    span_.start_time = std::chrono::steady_clock::now();
  }

  SpanBuilder& SetParent(const SpanContext& parent) {
    span_.context.parent_span_id = parent.span_id;
    span_.context.trace_id = parent.trace_id;
    return *this;
  }

  SpanBuilder& SetTag(const std::string& key, const std::string& value) {
    span_.tags[key] = value;
    return *this;
  }

  SpanBuilder& SetTag(const std::string& key, int64_t value) {
    span_.tags[key] = std::to_string(value);
    return *this;
  }

  SpanBuilder& SetOperation(const std::string& op) {
    span_.operation = op;
    return *this;
  }

  // 开始 span，返回 Span 对象
  Span Start();

 private:
  Tracer* tracer_;
  Span span_;
};

// ==================== Tracer 接口 ====================

class Tracer {
 public:
  virtual ~Tracer() = default;

  // 开始新的 Span
  virtual Span StartSpan(const std::string& name) = 0;

  // 基于父 Span 开始
  virtual Span StartSpan(const std::string& name, const SpanContext& parent) = 0;

  // 获取当前活跃 Span
  virtual std::optional<SpanContext> GetCurrentSpan() const = 0;

  // 导出所有 Span
  virtual std::vector<Span> GetSpans() const = 0;

  // 导出为 OTLP JSON 格式
  virtual std::string ExportOTLP() const = 0;

  // 序列化 SpanContext 以便跨线程/跨服务传递
  static std::string SerializeContext(const SpanContext& ctx);
  static std::optional<SpanContext> DeserializeContext(const std::string& data);

  // 生成 Trace ID
  static std::string GenerateTraceId();
  static std::string GenerateSpanId();
};

// ==================== 本地 Tracer 实现 ====================

class LocalTracer : public Tracer {
 public:
  explicit LocalTracer(size_t max_spans = 10000);
  ~LocalTracer() override = default;

  Span StartSpan(const std::string& name) override;
  Span StartSpan(const std::string& name, const SpanContext& parent) override;
  std::optional<SpanContext> GetCurrentSpan() const override;
  std::vector<Span> GetSpans() const override;
  std::string ExportOTLP() const override;

  void Clear();
  size_t SpanCount() const;

 private:
  std::vector<std::unique_ptr<Span>> spans_;
  std::atomic<uint64_t> span_counter_{0};
  size_t max_spans_;
  mutable std::mutex mutex_;

  // 每个线程的当前 span
  static thread_local std::string current_span_id_;
};

// ==================== RAII ScopedSpan ====================

class ScopedSpan {
 public:
  ScopedSpan(Tracer* tracer, std::string name)
      : tracer_(tracer), span_(tracer->StartSpan(std::move(name))) {}

  ScopedSpan(Tracer* tracer, std::string name, const SpanContext& parent)
      : tracer_(tracer), span_(tracer->StartSpan(std::move(name), parent)) {}

  ~ScopedSpan() { span_.End(); }

  Span& operator*() { return span_; }
  Span* operator->() { return &span_; }

 private:
  Tracer* tracer_;
  Span span_;
};

// ==================== 追踪宏 ====================

#define TRACE_SPAN(tracer, name)                                        \
  poker_engine::observability::ScopedSpan _scoped_span((tracer), name); \
  _scoped_span->SetTag("component", __func__)

#define TRACE_ASYNC(tracer, name, parent_ctx)         \
  auto _span = (tracer)->StartSpan(name, parent_ctx); \
  _span.SetTag("async", "true")

}  // namespace poker_engine::observability
