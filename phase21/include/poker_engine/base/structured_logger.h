#pragma once

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

namespace poker_engine::base {

// ==================== 结构化日志器 ====================
// 基于 spdlog 的异步日志系统
// 输出 JSON 格式日志，兼容 ELK/Splunk/Grafana Loki

class StructuredLogger {
 public:
  enum class Level { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Critical = 5 };

  struct LogEntry {
    std::string timestamp;
    Level level;
    std::string logger_name;
    std::string message;
    nlohmann::json fields;
    std::string thread_id;
    std::string file;
    int line;
  };

  // 获取 Logger 实例
  template <typename... Args>
  static std::shared_ptr<spdlog::logger> Create(const std::string& name, Args&&... args) {
    return spdlog::get(name);
  }

  // 初始化全局日志系统
  static void Initialize(const std::string& log_dir = "./logs", Level console_level = Level::Info,
                         Level file_level = Level::Debug,
                         size_t max_file_size = 10 * 1024 * 1024,  // 10MB
                         size_t max_files = 5);

  // 获取命名 Logger
  static std::shared_ptr<spdlog::logger> Get(const std::string& name) { return spdlog::get(name); }

  // JSON 格式输出
  static std::string ToJson(const LogEntry& entry);

  // Flush all loggers
  static void Flush() { spdlog::flush_every(std::chrono::seconds(1)); }

  // Shutdown
  static void Shutdown() { spdlog::shutdown(); }
};

// ==================== 游戏日志记录器 ====================

class GameLogger {
 public:
  explicit GameLogger(std::string logger_name) : logger_(spdlog::get(logger_name)) {
    if (!logger_) {
      logger_ = spdlog::stdout_color_mt(logger_name);
    }
  }

  // 带结构化字段的日志
  void LogGameEvent(const std::string& event_type, int64_t player_id, const std::string& table_id,
                    const nlohmann::json& details = {}) {
    nlohmann::json log_entry;
    log_entry["event"] = event_type;
    log_entry["player_id"] = player_id;
    log_entry["table_id"] = table_id;
    log_entry["timestamp"] = GetTimestamp();
    log_entry["details"] = details;

    // 写入游戏日志文件
    game_log_file_ << log_entry.dump() << "\n";
    game_log_file_.flush();
  }

  void LogAction(int64_t player_id, const std::string& table_id, const std::string& action,
                 int64_t amount) {
    nlohmann::json details;
    details["action"] = action;
    details["amount"] = amount;
    LogGameEvent("player_action", player_id, table_id, details);
  }

  void LogHandResult(int64_t hand_id, const std::vector<std::pair<int64_t, int64_t>>& payouts) {
    nlohmann::json details;
    details["hand_id"] = hand_id;
    for (auto& [pid, amount] : payouts) {
      details["payouts"].push_back({{"player_id", pid}, {"amount", amount}});
    }
    LogGameEvent("hand_complete", 0, "", details);
  }

 private:
  std::shared_ptr<spdlog::logger> logger_;
  std::ofstream game_log_file_;

  static std::string GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%S");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
  }
};

// ==================== 日志宏 ====================

#ifndef LOG_TRACE
#define LOG_TRACE(...) SPDLOG_LOGGER_TRACE(spdlog::default_logger(), __VA_ARGS__)
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(spdlog::default_logger(), __VA_ARGS__)
#endif
#ifndef LOG_INFO
#define LOG_INFO(...) SPDLOG_LOGGER_INFO(spdlog::default_logger(), __VA_ARGS__)
#endif
#ifndef LOG_WARN
#define LOG_WARN(...) SPDLOG_LOGGER_WARN(spdlog::default_logger(), __VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(...) SPDLOG_LOGGER_ERROR(spdlog::default_logger(), __VA_ARGS__)
#endif
#ifndef LOG_CRITICAL
#define LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(spdlog::default_logger(), __VA_ARGS__)
#endif

// 带游戏上下文的日志宏
#define GAME_LOG(logger, event, player, table, ...)            \
  do {                                                         \
    nlohmann::json __gl_details = {__VA_ARGS__};               \
    (logger).LogGameEvent(event, player, table, __gl_details); \
  } while (0)

}  // namespace poker_engine::base
