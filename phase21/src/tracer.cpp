#include "poker_engine/observability/tracer.h"

#include <iomanip>
#include <random>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::observability {

thread_local std::string LocalTracer::current_span_id_;

// ==================== LocalTracer ====================

LocalTracer::LocalTracer(size_t max_spans) : max_spans_(max_spans) {
  spans_.reserve(max_spans);
  PE_LOG_INFO("LocalTracer initialized (max_spans={})", max_spans);
}

Span LocalTracer::StartSpan(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto span = std::make_unique<Span>();
  span->context.trace_id = current_span_id_.empty() ? GenerateTraceId() : span->context.trace_id;
  span->context.span_id = GenerateSpanId();
  span->context.parent_span_id = current_span_id_;
  span->name = name;
  span->start_time = std::chrono::steady_clock::now();

  spans_.push_back(std::move(span));

  // 设置当前 span
  current_span_id_ = spans_.back()->context.span_id;
  spans_.back()->context.trace_id =
      current_span_id_.empty() ? GenerateTraceId() : spans_.back()->context.trace_id;

  return *spans_.back();
}

Span LocalTracer::StartSpan(const std::string& name, const SpanContext& parent) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto span = std::make_unique<Span>();
  span->context.trace_id = parent.trace_id.empty() ? GenerateTraceId() : parent.trace_id;
  span->context.span_id = GenerateSpanId();
  span->context.parent_span_id = parent.span_id;
  span->name = name;
  span->start_time = std::chrono::steady_clock::now();

  spans_.push_back(std::move(span));

  return *spans_.back();
}

std::optional<SpanContext> LocalTracer::GetCurrentSpan() const {
  if (current_span_id_.empty()) return std::nullopt;

  SpanContext ctx;
  ctx.span_id = current_span_id_;
  return ctx;
}

std::vector<Span> LocalTracer::GetSpans() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<Span> result;
  result.reserve(spans_.size());
  for (auto& s : spans_) result.push_back(*s);
  return result;
}

std::string LocalTracer::ExportOTLP() const {
  auto spans = GetSpans();

  nlohmann::json json;
  json["resourceSpans"] = nlohmann::json::array();

  nlohmann::json resource_span;
  resource_span["resource"]["attributes"] = {{"service.name", "poker-engine"},
                                             {"service.version", "0.4.0"}};

  nlohmann::json scope_spans;
  scope_spans["scope"]["name"] = "poker-engine";

  nlohmann::json spans_json = nlohmann::json::array();
  for (auto& span : spans) {
    nlohmann::json span_json;
    span_json["traceId"] = span.context.trace_id;
    span_json["spanId"] = span.context.span_id;
    if (!span.context.parent_span_id.empty())
      span_json["parentSpanId"] = span.context.parent_span_id;
    span_json["name"] = span.name;
    span_json["kind"] = "SPAN_KIND_INTERNAL";

    span_json["startTimeUnixNano"] =
        std::chrono::duration_cast<std::chrono::nanoseconds>(span.start_time.time_since_epoch())
            .count();
    span_json["endTimeUnixNano"] =
        std::chrono::duration_cast<std::chrono::nanoseconds>(span.end_time.time_since_epoch())
            .count();

    // Tags
    if (!span.tags.empty()) {
      for (auto& [k, v] : span.tags) {
        span_json["attributes"].push_back({{"key", k}, {"value", {"stringValue", v}}});
      }
    }

    // Events
    for (auto& event : span.events) {
      span_json["events"].push_back(
          {{"name", event},
           {"timeUnixNano", std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count()}});
    }

    span_json["status"] = {"code", "STATUS_CODE_OK"};
    spans_json.push_back(span_json);
  }

  scope_spans["spans"] = spans_json;
  resource_span["scopeSpans"].push_back(scope_spans);
  json["resourceSpans"].push_back(resource_span);

  return json.dump(2);
}

void LocalTracer::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  spans_.clear();
  span_counter_ = 0;
}

size_t LocalTracer::SpanCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return spans_.size();
}

// ==================== Trace ID 生成 ====================

std::string Tracer::GenerateTraceId() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dist;

  std::ostringstream oss;
  oss << std::hex << std::setfill('0') << std::setw(16) << dist(gen) << std::setw(16) << dist(gen);
  return oss.str();
}

std::string Tracer::GenerateSpanId() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dist;

  std::ostringstream oss;
  oss << std::hex << std::setfill('0') << std::setw(16) << dist(gen);
  return oss.str();
}

std::string Tracer::SerializeContext(const SpanContext& ctx) {
  nlohmann::json j;
  j["traceId"] = ctx.trace_id;
  j["spanId"] = ctx.span_id;
  j["parentSpanId"] = ctx.parent_span_id;
  return j.dump();
}

std::optional<SpanContext> Tracer::DeserializeContext(const std::string& data) {
  try {
    auto j = nlohmann::json::parse(data);
    SpanContext ctx;
    ctx.trace_id = j.value("traceId", "");
    ctx.span_id = j.value("spanId", "");
    ctx.parent_span_id = j.value("parentSpanId", "");
    if (ctx.trace_id.empty()) return std::nullopt;
    return ctx;
  } catch (...) {
    return std::nullopt;
  }
}

Span SpanBuilder::Start() {
  if (tracer_) {
    if (span_.context.parent_span_id.empty()) {
      return tracer_->StartSpan(span_.name);
    } else {
      return tracer_->StartSpan(span_.name, span_.context);
    }
  }
  return span_;
}

}  // namespace poker_engine::observability
