#pragma once
#include <fmt/format.h>

#include <string>
#include <string_view>

namespace poker_engine {

enum class LogLevel { Debug, Info, Warning, Error };

class Logger {
 public:
  static Logger& Instance();
  void SetLevel(LogLevel level) { level_ = level; }
  void Debug(std::string_view msg);
  void Info(std::string_view msg);
  void Warning(std::string_view msg);
  void Error(std::string_view msg);

 private:
  LogLevel level_ = LogLevel::Info;
};

}  // namespace poker_engine

// Variadic logging macros using fmt::format.
// PE_LOG_INFO("hello {}", name)  → Logger::Info(fmt::format("hello {}", name))
// PE_LOG_INFO("simple msg")     → Logger::Info("simple msg")
#define PE_LOG_DEBUG(...) poker_engine::Logger::Instance().Debug(fmt::format(__VA_ARGS__))
#define PE_LOG_INFO(...) poker_engine::Logger::Instance().Info(fmt::format(__VA_ARGS__))
#define PE_LOG_WARN(...) poker_engine::Logger::Instance().Warning(fmt::format(__VA_ARGS__))
#define PE_LOG_ERROR(...) poker_engine::Logger::Instance().Error(fmt::format(__VA_ARGS__))
