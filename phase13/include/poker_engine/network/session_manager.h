#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "poker_engine/game/game_state.h"

namespace poker_engine::network {

struct Session {
  std::string token;
  int32_t player_id;
  std::string table_id;
  bool is_connected = false;
  std::chrono::steady_clock::time_point last_active;
  std::string pending_actions;  // JSON array of actions taken while disconnected

  void Touch() { last_active = std::chrono::steady_clock::now(); }
  bool IsExpired(std::chrono::seconds timeout = std::chrono::seconds(60)) const {
    return std::chrono::steady_clock::now() - last_active > timeout;
  }
};

class SessionManager {
 public:
  SessionManager(std::chrono::seconds timeout = std::chrono::seconds(60));

  // Create a new session when player joins
  std::string Create(int32_t player_id, const std::string& table_id);

  // Get session by token
  std::optional<Session> Get(const std::string& token) const;

  // Mark as connected with WS
  bool Connect(const std::string& token);

  // Mark as disconnected (keep session for reconnection window)
  bool Disconnect(const std::string& token);

  // Reconnect existing session
  bool Reconnect(const std::string& token);

  // Remove expired sessions and fold their pending actions
  int CleanupExpired();

  // Check if session is valid and owns this table
  bool IsAuthorized(const std::string& token, const std::string& table_id) const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Session> sessions_;
  std::chrono::seconds timeout_;

  std::string GenerateToken();
};

}  // namespace poker_engine::network
