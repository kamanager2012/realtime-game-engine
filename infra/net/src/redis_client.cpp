#include "poker_engine/network/redis_client.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

namespace poker_engine::network {
namespace {

bool SetTimeout(int fd, int timeout_ms) {
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
         setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

bool WriteAll(int fd, const char* data, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t n = send(fd, data + off, len - off, 0);
    if (n <= 0) return false;
    off += static_cast<size_t>(n);
  }
  return true;
}

bool ReadAll(int fd, char* data, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t n = recv(fd, data + off, len - off, 0);
    if (n <= 0) return false;
    off += static_cast<size_t>(n);
  }
  return true;
}

}  // namespace

RedisClient::~RedisClient() { Disconnect(); }

bool RedisClient::IsCircuitOpen() const {
  if (consecutive_failures_ < config_.circuit_breaker_threshold) return false;
  auto elapsed = std::chrono::steady_clock::now() - last_failure_time_;
  return elapsed < std::chrono::milliseconds(config_.circuit_breaker_cooldown_ms);
}

bool RedisClient::TryReconnect() {
  if (IsConnected()) return true;
  if (IsCircuitOpen()) return false;  // don't hammer a dead server

  auto start = std::chrono::steady_clock::now();
  auto max_wait = std::chrono::milliseconds(30000);  // 30s cap
  int attempt = 0;

  while (true) {
    if (Connect(config_)) {
      consecutive_failures_ = 0;  // reset on success
      return true;
    }
    consecutive_failures_++;
    last_failure_time_ = std::chrono::steady_clock::now();

    // Exponential backoff: 100ms * 2^attempt, max 5s
    int delay_ms = std::min(5000, 100 * (1 << std::min(attempt, 5)));
    if (std::chrono::steady_clock::now() - start + std::chrono::milliseconds(delay_ms) > max_wait) {
      return false;  // exceeded max wait, open circuit
    }
    usleep(delay_ms * 1000);
    attempt++;
  }
}

bool RedisClient::ParseUrl(const std::string& url, RedisConfig& out) {
  std::string u = url;
  if (u.rfind("redis://", 0) == 0) u = u.substr(8);
  auto colon = u.rfind(':');
  if (colon == std::string::npos) {
    out.host = u;
    out.port = 6379;
    return true;
  }
  out.host = u.substr(0, colon);
  out.port = std::stoi(u.substr(colon + 1));
  return !out.host.empty() && out.port > 0;
}

bool RedisClient::Connect(const RedisConfig& config) {
  Disconnect();
  config_ = config;
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (fd_ < 0) return false;
  if (!SetTimeout(fd_, config_.connect_timeout_ms)) {
    Disconnect();
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(config_.port));
  if (inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr) != 1) {
    Disconnect();
    return false;
  }
  if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    Disconnect();
    return false;
  }
  SetTimeout(fd_, config_.io_timeout_ms);
  return Ping();
}

void RedisClient::Disconnect() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

bool RedisClient::SendCommand(const std::vector<std::string>& parts) {
  if (fd_ < 0 && !TryReconnect()) return false;
  std::string cmd = "*" + std::to_string(parts.size()) + "\r\n";
  for (const auto& p : parts) {
    cmd += "$" + std::to_string(p.size()) + "\r\n" + p + "\r\n";
  }
  if (!WriteAll(fd_, cmd.c_str(), cmd.size())) {
    Disconnect();
    consecutive_failures_++;
    last_failure_time_ = std::chrono::steady_clock::now();
    return false;
  }
  return true;
}

bool RedisClient::ReadLine(std::string& line) {
  line.clear();
  char ch = 0;
  while (true) {
    if (!ReadAll(fd_, &ch, 1)) return false;
    if (ch == '\n') break;
    if (ch != '\r') line.push_back(ch);
  }
  return true;
}

bool RedisClient::ReadSimpleString(std::string& out) {
  std::string line;
  if (!ReadLine(line) || line.empty()) return false;
  if (line[0] == '-') return false;
  out = line.substr(1);
  return true;
}

bool RedisClient::ReadBulkString(std::optional<std::string>& out) {
  std::string line;
  if (!ReadLine(line) || line.empty()) return false;
  if (line[0] == '$') {
    int len = std::stoi(line.substr(1));
    if (len < 0) {
      out = std::nullopt;
      return true;
    }
    std::string data(len, '\0');
    if (!ReadAll(fd_, data.data(), static_cast<size_t>(len))) return false;
    char crlf[2];
    if (!ReadAll(fd_, crlf, 2)) return false;
    out = data;
    return true;
  }
  if (line[0] == '-') return false;
  out = line.substr(1);
  return true;
}

bool RedisClient::Ping() {
  if (!SendCommand({"PING"})) return false;
  std::string pong;
  return ReadSimpleString(pong) && pong == "PONG";
}

bool RedisClient::Set(const std::string& key, const std::string& value, int ttl_seconds) {
  std::vector<std::string> parts = {"SET", key, value};
  if (ttl_seconds > 0) {
    parts.push_back("EX");
    parts.push_back(std::to_string(ttl_seconds));
  }
  if (!SendCommand(parts)) return false;
  std::string resp;
  return ReadSimpleString(resp) && resp == "OK";
}

std::optional<std::string> RedisClient::Get(const std::string& key) {
  if (!SendCommand({"GET", key})) return std::nullopt;
  std::optional<std::string> value;
  if (!ReadBulkString(value)) return std::nullopt;
  return value;
}

bool RedisClient::Del(const std::string& key) {
  if (!SendCommand({"DEL", key})) return false;
  std::string line;
  if (!ReadLine(line) || line.empty() || line[0] != ':') return false;
  return true;
}

}  // namespace poker_engine::network
