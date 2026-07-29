#pragma once
#include <openssl/rand.h>

#include <chrono>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace poker_engine::network {

// ==================== Token 认证管理器 ====================
// 生成和管理 session tokens
// 生产环境应替换为 JWT

class AuthManager {
 public:
  struct SessionInfo {
    int64_t player_id;
    int64_t session_id;
    int64_t table_id = -1;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point expires_at;
  };

  // 为玩家创建新 token
  std::string CreateToken(int64_t player_id, int64_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string token = GenerateRandomToken();
    SessionInfo info;
    info.player_id = player_id;
    info.session_id = session_id;
    info.created_at = std::chrono::steady_clock::now();
    info.expires_at = info.created_at + std::chrono::hours(24);

    token_to_info_[token] = info;
    player_to_token_[player_id] = token;

    return token;
  }

  // 验证 token
  std::optional<SessionInfo> VerifyToken(const std::string& token) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = token_to_info_.find(token);
    if (it == token_to_info_.end()) return std::nullopt;

    if (std::chrono::steady_clock::now() > it->second.expires_at) {
      return std::nullopt;  // 已过期
    }

    return it->second;
  }

  // 撤销 token（登出）
  void RevokeToken(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = token_to_info_.find(token);
    if (it != token_to_info_.end()) {
      player_to_token_.erase(it->second.player_id);
      token_to_info_.erase(it);
    }
  }

  // 更新 session 绑定
  void UpdateSession(int64_t player_id, int64_t new_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = player_to_token_.find(player_id);
    if (it != player_to_token_.end()) {
      token_to_info_[it->second].session_id = new_session_id;
    }
  }

  // 更新桌子绑定
  void SetPlayerTable(int64_t player_id, int64_t table_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto pit = player_to_token_.find(player_id);
    if (pit != player_to_token_.end()) {
      auto tit = token_to_info_.find(pit->second);
      if (tit != token_to_info_.end()) {
        tit->second.table_id = table_id;
      }
    }
  }

 private:
  static std::string GenerateRandomToken() {
    // CSPRNG only: session tokens are bearer credentials. mt19937_64 seeded
    // from a single random_device u32 has ~32 bits of effective entropy and
    // is predictable — session hijacking territory. RAND_bytes is OS entropy.
    unsigned char raw[16];
    if (RAND_bytes(raw, sizeof(raw)) != 1) {
      throw std::runtime_error("auth: RAND_bytes failed - no entropy available");
    }
    const char* hex = "0123456789abcdef";
    std::string token(32, '0');
    for (int i = 0; i < 16; ++i) {
      token[2 * i] = hex[raw[i] >> 4];
      token[2 * i + 1] = hex[raw[i] & 0xF];
    }
    return token;
  }

  mutable std::mutex mutex_;
  std::unordered_map<std::string, SessionInfo> token_to_info_;
  std::unordered_map<int64_t, std::string> player_to_token_;
};

}  // namespace poker_engine::network
