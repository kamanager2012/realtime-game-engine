#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace poker_engine::network {

struct DisconnectConfig {
  int reconnect_timeout_seconds = 120;
  int reconnect_warning_seconds = 30;
  int max_reconnect_attempts = 3;
  bool fold_on_timeout = true;
  bool spectator_mode = true;
  bool preserve_seat = true;
};

struct PlayerConnectionState {
  int64_t player_id;
  std::string session_token;
  std::chrono::steady_clock::time_point disconnect_time;
  int reconnect_attempts = 0;
  bool notified_warning = false;
  bool is_disconnected = false;
  bool timed_out = false;

  int RemainingSeconds(int timeout) const {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::steady_clock::now() - disconnect_time)
                       .count();
    return std::max(0, timeout - static_cast<int>(elapsed));
  }

  bool IsTimedOut(int timeout_seconds) const { return RemainingSeconds(timeout_seconds) <= 0; }
};

class ReconnectionManager {
 public:
  explicit ReconnectionManager(const DisconnectConfig& config = DisconnectConfig());
  ~ReconnectionManager();

  void MarkDisconnected(int64_t player_id, const std::string& session_token);
  bool MarkReconnected(int64_t player_id, const std::string& session_token);

  bool IsDisconnected(int64_t player_id) const;
  bool IsTimedOut(int64_t player_id) const;
  int GetRemainingSeconds(int64_t player_id) const;

  void Start();
  void Stop();

  void SetTimeoutCallback(std::function<void(int64_t)> cb) { timeout_callback_ = std::move(cb); }
  void SetReconnectCallback(std::function<void(int64_t)> cb) {
    reconnect_callback_ = std::move(cb);
  }

  std::vector<PlayerConnectionState> GetDisconnectedPlayers() const;

 private:
  void MonitorLoop();

  DisconnectConfig config_;
  std::unordered_map<int64_t, PlayerConnectionState> connections_;
  mutable std::shared_mutex mutex_;

  std::atomic<bool> running_{false};
  std::thread monitor_thread_;
  std::condition_variable_any cv_;

  std::function<void(int64_t)> timeout_callback_;
  std::function<void(int64_t)> reconnect_callback_;
};

}  // namespace poker_engine::network
