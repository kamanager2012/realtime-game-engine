#include "poker_engine/network/session_manager.h"

#include <iomanip>
#include <random>
#include <sstream>

namespace poker_engine::network {

#include <openssl/rand.h>
#include <sstream>
#include "poker_engine/network/session_manager.h"

namespace {
std::string RandomToken() {
  unsigned char buf[16];
  if (RAND_bytes(buf, sizeof(buf)) != 1) {
    // Fallback: use time + counter (emergency only)
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << counter++ << now;
    return oss.str();
  }
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (int i = 0; i < 16; ++i) oss << std::setw(2) << static_cast<int>(buf[i]);
  return oss.str();
}
}  // namespace

SessionManager::SessionManager(std::chrono::seconds timeout) : timeout_(timeout) {}

std::string SessionManager::Create(int32_t player_id, const std::string& table_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string token = RandomToken();

  Session session;
  session.token = token;
  session.player_id = player_id;
  session.table_id = table_id;
  session.is_connected = false;
  session.Touch();
  sessions_[token] = session;
  return token;
}

std::optional<Session> SessionManager::Get(const std::string& token) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(token);
  if (it != sessions_.end()) return it->second;
  return std::nullopt;
}

bool SessionManager::Connect(const std::string& token) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(token);
  if (it == sessions_.end()) return false;
  it->second.is_connected = true;
  it->second.Touch();
  return true;
}

bool SessionManager::Disconnect(const std::string& token) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(token);
  if (it == sessions_.end()) return false;
  it->second.is_connected = false;
  it->second.Touch();
  return true;
}

bool SessionManager::Reconnect(const std::string& token) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(token);
  if (it == sessions_.end()) return false;
  if (it->second.IsExpired(timeout_)) return false;
  it->second.is_connected = true;
  it->second.Touch();
  return true;
}

int SessionManager::CleanupExpired() {
  std::lock_guard<std::mutex> lock(mutex_);
  int removed = 0;
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (!it->second.is_connected && it->second.IsExpired(timeout_)) {
      it = sessions_.erase(it);
      removed++;
    } else {
      ++it;
    }
  }
  return removed;
}

bool SessionManager::IsAuthorized(const std::string& token, const std::string& table_id) const {
  auto session = Get(token);
  if (!session.has_value()) return false;
  return session->table_id == table_id && session->is_connected;
}

std::string SessionManager::GenerateToken() { return RandomToken(); }

}  // namespace poker_engine::network
