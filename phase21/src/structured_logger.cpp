#include "poker_engine/base/structured_logger.h"

#include <spdlog/async.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "poker_engine/base/logging.h"

namespace poker_engine::base {

void StructuredLogger::Initialize(const std::string& log_dir, Level console_level, Level file_level,
                                  size_t max_file_size, size_t max_files) {
  // 创建异步日志线程池
  spdlog::init_thread_pool(8192, 2);  // 8K 队列, 2 个后台线程

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

  // 控制台输出（彩色）
  console_sink->set_level(static_cast<spdlog::level::level_enum>(console_level));
  console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");

  // 文件输出（JSON 格式）
  auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      log_dir + "/poker_engine.log", max_file_size, max_files);
  file_sink->set_level(static_cast<spdlog::level::level_enum>(file_level));

  // JSON 格式
  file_sink->set_pattern(
      R"({"timestamp":"%Y-%m-%dT%H:%M:%S.%eZ","level":"%l","logger":"%n","thread":%t,"message":"%v"})");

  // 创建主 Logger
  auto main_logger = std::make_shared<spdlog::async_logger>(
      "poker_engine", spdlog::sinks_init_list({console_sink, file_sink}), spdlog::thread_pool(),
      spdlog::async_overflow_policy::block);

  spdlog::register_logger(main_logger);
  spdlog::set_default_logger(main_logger);

  // 安全性 Logger（单独的 Sink）
  auto security_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      log_dir + "/security.log", max_file_size, max_files);
  security_file_sink->set_pattern(
      R"({"timestamp":"%Y-%m-%dT%H:%M:%S.%eZ","level":"%l","logger":"security","message":"%v"})");

  auto security_logger =
      std::make_shared<spdlog::async_logger>("security", security_file_sink, spdlog::thread_pool());
  spdlog::register_logger(security_logger);

  // 游戏日志（高吞吐专用）
  auto game_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      log_dir + "/game.log", max_file_size, max_files);
  game_file_sink->set_pattern("%v");  // 纯 JSON 格式

  auto game_logger =
      std::make_shared<spdlog::async_logger>("game", game_file_sink, spdlog::thread_pool());
  spdlog::register_logger(game_logger);

  // 网络日志
  auto network_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      log_dir + "/network.log", max_file_size, max_files);
  network_file_sink->set_pattern(
      R"({"timestamp":"%Y-%m-%dT%H:%M:%S.%eZ","level":"%l","logger":"network","data":%v})");

  auto network_logger =
      std::make_shared<spdlog::async_logger>("network", network_file_sink, spdlog::thread_pool());
  spdlog::register_logger(network_logger);

  PE_LOG_INFO("Structured logger initialized (console_level={}, file_level={})",
              static_cast<int>(console_level), static_cast<int>(file_level));
}

std::string StructuredLogger::ToJson(const LogEntry& entry) {
  nlohmann::json j;
  j["timestamp"] = entry.timestamp;
  j["level"] = static_cast<int>(entry.level);
  j["logger"] = entry.logger_name;
  j["message"] = entry.message;
  j["fields"] = entry.fields;
  j["thread"] = entry.thread_id;
  j["source"] = entry.file + ":" + std::to_string(entry.line);
  return j.dump();
}

}  // namespace poker_engine::base
