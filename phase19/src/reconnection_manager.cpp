#include "poker_engine/network/reconnection_manager.h"

#include "poker_engine/base/logging.h"

namespace poker_engine::network {

ReconnectionManager::ReconnectionManager(const DisconnectConfig& config) : config_(config) {}

ReconnectionManager::~ReconnectionManager() { Stop(); }

void ReconnectionManager::MarkDisconnected(int64_t player_id, const std::string& session_token) {
  std::unique_lock lock(mutex_);

  auto& state = connections_[player_id];
  state.player_id = player_id;
  state.session_token = session_token;
  state.disconnect_time = std::chrono::steady_clock::now();
  state.is_disconnected = true;
  state.timed_out = false;
  state.notified_warning = false;

  PE_LOG_INFO("Player {} disconnected (timeout: {}s)", player_id,
              config_.reconnect_timeout_seconds);
}

bool ReconnectionManager::MarkReconnected(int64_t player_id, const std::string& session_token) {
  std::unique_lock lock(mutex_);

  auto it = connections_.find(player_id);
  if (it == connections_.end()) return false;

  auto& state = it->second;

  if (state.timed_out) {
    PE_LOG_WARN("Player {} reconnected after timeout", player_id);
  }

  state.is_disconnected = false;
  state.reconnect_attempts++;

  PE_LOG_INFO("Player {} reconnected (attempt #{})", player_id, state.reconnect_attempts);

  lock.unlock();

  if (reconnect_callback_) {
    reconnect_callback_(player_id);
  }

  std::unique_lock lock2(mutex_);
  connections_.erase(player_id);
  return true;
}

bool ReconnectionManager::IsDisconnected(int64_t player_id) const {
  std::shared_lock lock(mutex_);
  auto it = connections_.find(player_id);
  return it != connections_.end() && it->second.is_disconnected;
}

bool ReconnectionManager::IsTimedOut(int64_t player_id) const {
  std::shared_lock lock(mutex_);
  auto it = connections_.find(player_id);
  if (it == connections_.end()) return false;
  return it->second.IsTimedOut(config_.reconnect_timeout_seconds);
}

int ReconnectionManager::GetRemainingSeconds(int64_t player_id) const {
  std::shared_lock lock(mutex_);
  auto it = connections_.find(player_id);
  if (it == connections_.end()) return -1;
  return it->second.RemainingSeconds(config_.reconnect_timeout_seconds);
}

void ReconnectionManager::Start() {
  if (running_) return;
  running_ = true;
  monitor_thread_ = std::thread(&ReconnectionManager::MonitorLoop, this);
  PE_LOG_INFO("Reconnection manager started");
}

void ReconnectionManager::Stop() {
  running_ = false;
  cv_.notify_all();
  if (monitor_thread_.joinable()) {
    monitor_thread_.join();
  }
  PE_LOG_INFO("Reconnection manager stopped");
}

std::vector<PlayerConnectionState> ReconnectionManager::GetDisconnectedPlayers() const {
  std::shared_lock lock(mutex_);
  std::vector<PlayerConnectionState> result;
  for (auto& [id, state] : connections_) {
    if (state.is_disconnected) {
      result.push_back(state);
    }
  }
  return result;
}

void ReconnectionManager::MonitorLoop() {
  using namespace std::chrono_literals;

  while (running_) {
    std::vector<int64_t> timed_out;

    {
      std::unique_lock lock(mutex_);
      for (auto& [id, state] : connections_) {
        if (!state.is_disconnected) continue;

        int remaining = state.RemainingSeconds(config_.reconnect_timeout_seconds);

        if (!state.notified_warning && remaining <= config_.reconnect_warning_seconds) {
          PE_LOG_WARN("Player {} has {}s to reconnect", id, remaining);
          state.notified_warning = true;
        }

        if (remaining <= 0) {
          timed_out.push_back(id);
        }
      }
    }

    for (auto id : timed_out) {
      PE_LOG_WARN("Player {} timed out - taking action", id);

      if (timeout_callback_) {
        timeout_callback_(id);
      }

      std::unique_lock lock(mutex_);
      auto it = connections_.find(id);
      if (it != connections_.end()) {
        it->second.timed_out = true;
        if (!config_.preserve_seat) {
          connections_.erase(it);
        }
      }
    }

    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, 1s, [this]() { return !running_; });
  }
}

}  // namespace poker_engine::network
