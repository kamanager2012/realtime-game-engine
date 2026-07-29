#include "poker_engine/base/logging.h"

#include <iostream>
#include <mutex>

namespace poker_engine {
std::mutex g_log_mutex;
void Logger::Debug(std::string_view msg) {
  if (level_ <= LogLevel::Debug) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::clog << "[DEBUG] " << msg << "\n";
  }
}
void Logger::Info(std::string_view msg) {
  if (level_ <= LogLevel::Info) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << "[INFO]  " << msg << "\n";
  }
}
void Logger::Warning(std::string_view msg) {
  if (level_ <= LogLevel::Warning) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cerr << "[WARN]  " << msg << "\n";
  }
}
void Logger::Error(std::string_view msg) {
  if (level_ <= LogLevel::Error) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cerr << "[ERROR] " << msg << "\n";
  }
}
Logger& Logger::Instance() {
  static Logger inst;
  return inst;
}
}  // namespace poker_engine
