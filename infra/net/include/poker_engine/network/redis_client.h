#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>
#include <string>

namespace poker_engine::network {

struct RedisConfig {
  std::string host = "127.0.0.1";
  int port = 6379;
  int connect_timeout_ms = 2000;
  int io_timeout_ms = 2000;
  int circuit_breaker_threshold = 5;      // consecutive failures to open
  int circuit_breaker_cooldown_ms = 30000; // cooldown before retry
};

class RedisClient {
 public:
  RedisClient() = default;
  ~RedisClient();

  bool Connect(const RedisConfig& config);
  void Disconnect();
  bool IsConnected() const { return fd_ >= 0; }
  bool IsCircuitOpen() const;

  bool Ping();
  bool Set(const std::string& key, const std::string& value, int ttl_seconds = 0);
  std::optional<std::string> Get(const std::string& key);
  bool Del(const std::string& key);

  static bool ParseUrl(const std::string& url, RedisConfig& out);

 private:
  bool SendCommand(const std::vector<std::string>& parts);
  bool ReadLine(std::string& line);
  bool ReadSimpleString(std::string& out);
  bool ReadBulkString(std::optional<std::string>& out);
  bool TryReconnect();

  int fd_ = -1;
  RedisConfig config_;
  int consecutive_failures_ = 0;
  std::chrono::steady_clock::time_point last_failure_time_;
};

}  // namespace poker_engine::network
