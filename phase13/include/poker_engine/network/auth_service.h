#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <memory>
#include <unordered_set>

#include "poker_engine/game/player_state.h"

namespace poker_engine::phase14 { class IAccountRepository; class AccountRepository; }

namespace poker_engine::network {

// ==================== 鐜╁璐︽埛 ====================

// Player roles for access control.
enum class PlayerRole : uint8_t { Player = 0, Moderator = 1, Admin = 2 };

struct PlayerAccount {
  int64_t id;
  std::string username;
  std::string display_name;
  std::string password_hash;
  std::string avatar_url;
  int64_t chips;
  int64_t total_profit;
  int64_t hands_played;
  int64_t elo_rating;
  PlayerRole role = PlayerRole::Player;
  std::string salt;
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point last_login;
};

// ==================== 璁よ瘉缁撴灉 ====================

struct AuthResult {
  bool success;
  std::string token;  // JWT-like token
  std::string error_message;
  int64_t player_id;
};

// ==================== JWT Token ====================

class TokenRevocationBackend {
 public:
  virtual ~TokenRevocationBackend() = default;
  virtual bool IsRevoked(const std::string& token) const = 0;
  virtual void Revoke(const std::string& token, int ttl_seconds) = 0;
};

class TokenService {
 public:
  explicit TokenService(const std::string& secret_key,
                        std::chrono::hours expiry = std::chrono::hours(24));

  // 鐢熸垚 token
  std::string Generate(int64_t player_id, const std::string& username);

  // 楠岃瘉 token锛岃繑鍥? player_id锛堟棤鏁堣繑鍥? nullopt锛?
  std::optional<int64_t> Verify(const std::string& token) const;

  // 鍒锋柊 token
  std::optional<std::string> Refresh(const std::string& old_token);

  // 注销 token
  void Revoke(const std::string& token);

  // 批量注销（玩家登出所有设备）
  // Stores a revocation timestamp: any token issued before this time is invalid.
  void RevokeAll(int64_t player_id);

  void SetRevocationBackend(std::shared_ptr<TokenRevocationBackend> backend);

 private:
  std::string secret_key_;
  std::chrono::hours expiry_;
  std::shared_ptr<TokenRevocationBackend> revocation_backend_;

  // 已注销但未过期的 token 黑名单
  mutable std::mutex blacklist_mutex_;
  std::unordered_set<std::string> blacklist_;

  // Player-level revocation: tokens with iat < this timestamp are invalid.
  mutable std::mutex player_revoke_mutex_;
  std::unordered_map<int64_t, int64_t> player_revoked_after_;

  std::string Base64Encode(const std::string& data) const;
  std::string Base64Decode(const std::string& data) const;
  std::string ComputeHMAC(const std::string& header_b64, const std::string& payload_b64) const;

 public:
  static std::string GenerateUUID();
};

// ==================== 璁よ瘉鏈嶅姟 ====================

class AuthService {
 public:
  explicit AuthService(std::string secret_key,
                        phase14::IAccountRepository* account_repo = nullptr);

  // 娉ㄥ唽鏂扮帺瀹?
  AuthResult Register(const std::string& username, const std::string& password,
                      const std::string& display_name = "");

  // 鐧诲綍
  AuthResult Login(const std::string& username, const std::string& password);

  // 閫氳繃 token 楠岃瘉韬唤
  AuthResult Authenticate(const std::string& token);

  // 鐧诲嚭
  bool Logout(const std::string& token);

  // 妫�鏌ョ敤鎴峰悕鏄惁瀛樺湪
  bool UsernameExists(const std::string& username) const;

  // 閫氳繃 ID 鑾峰彇鐜╁淇℃伅
  std::optional<PlayerAccount> GetPlayer(int64_t player_id) const;

  // Sync in-memory cache after ledger operations
  void SetPlayerChips(int64_t player_id, int64_t chips);

  // Role-based access control.
  PlayerRole GetPlayerRole(int64_t player_id) const;
  bool HasRole(int64_t player_id, PlayerRole required) const;
  void SetPlayerRole(int64_t player_id, PlayerRole role);

  // 璁剧疆鎸佷箙鍖栧悗绔紙鍙湪鏋勯�犲悗璋冪敤锛?
  void SetAccountRepository(phase14::IAccountRepository* repo);

  // 隐私删除后使内存缓存中的账号失效（下次登录需重新拉取；已匿名化则无法登录）
  void EvictPlayer(int64_t player_id);

  // 鑾峰彇 TokenService锛堜緵 WS 鏈嶅姟鍣ㄤ娇鐢級
  TokenService& GetTokenService() { return token_service_; }

 private:
  TokenService token_service_;

  phase14::IAccountRepository* account_repo_ = nullptr;

  // 鐜╁鏁版嵁搴擄紙鍐呭瓨缂撳瓨 鈥? 鐢熶骇鐜搴旂敱鏁版嵁搴? backing锛?
  mutable std::mutex players_mutex_;
  std::unordered_map<std::string, PlayerAccount> username_index_;  // username -> account
  std::unordered_map<int64_t, PlayerAccount> id_index_;            // id -> account
  int64_t next_player_id_ = 1;

  // 闃茬垎鐮达細鎸夌敤鎴峰悕璁板綍杩炵画澶辫触娆℃暟涓庨攣瀹氬埌鏈熸椂闂?
  struct FailedLogin {
    int attempts = 0;
    std::chrono::steady_clock::time_point lock_until;
  };
  mutable std::mutex lockout_mutex_;
  std::unordered_map<std::string, FailedLogin> failed_logins_;
  static constexpr int kMaxLoginAttempts = 5;
  static constexpr int kLockoutMinutes = 10;

  // 瀵嗙爜鍝堝笇
  static std::string HashPassword(const std::string& password);
  static bool VerifyPassword(const std::string& password, const std::string& hash);

  // Token 绛惧悕杈呭姪
  static std::string GenerateUUID();
};

}  // namespace poker_engine::network
