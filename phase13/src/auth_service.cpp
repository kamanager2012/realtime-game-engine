#include "poker_engine/network/auth_service.h"

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

#include "poker_engine/base/logging.h"
#include "nlohmann/json.hpp"

#include "poker_engine/phase14/account_repository.h"

namespace poker_engine::network {

namespace {

// ==================== Base64 编解码 ====================

std::string Base64EncodeImpl(const std::string& data) {
  BIO* b64 = BIO_new(BIO_f_base64());
  BIO* bio = BIO_new(BIO_s_mem());
  BIO_push(b64, bio);
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(b64, data.data(), static_cast<int>(data.size()));
  BIO_flush(b64);
  BUF_MEM* buf = nullptr;
  BIO_get_mem_ptr(b64, &buf);
  std::string result(buf->data, buf->length);
  BIO_free_all(b64);
  return result;
}

std::string Base64DecodeImpl(const std::string& data) {
  BIO* b64 = BIO_new(BIO_f_base64());
  BIO* bio = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
  BIO_push(b64, bio);
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  std::string result(data.size(), '\0');
  int decoded_len = BIO_read(b64, result.data(), static_cast<int>(result.size()));
  result.resize(decoded_len > 0 ? decoded_len : 0);
  BIO_free_all(b64);
  return result;
}

// ==================== 简单 PBKDF2 哈希（替代 bcrypt，减少依赖） ====================

std::string PBKDF2Hash(const std::string& password, const std::string& salt) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  // OWASP 2024 recommends ≥600,000 iterations for PBKDF2-SHA256.
  PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                    reinterpret_cast<const unsigned char*>(salt.c_str()),
                    static_cast<int>(salt.size()), 600000, EVP_sha256(), SHA256_DIGEST_LENGTH, hash);

  std::ostringstream oss;
  for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
  return oss.str();
}

std::string GenerateSalt() {
  unsigned char buf[16];
  if (RAND_bytes(buf, sizeof(buf)) != 1) {
    throw std::runtime_error("Auth: RAND_bytes failed — cannot generate secure salt");
  }
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (int i = 0; i < 16; ++i)
    oss << std::setw(2) << static_cast<int>(buf[i]);
  return oss.str();
}

}  // namespace

// ==================== TokenService ====================

TokenService::TokenService(const std::string& secret_key, std::chrono::hours expiry)
    : secret_key_(secret_key), expiry_(expiry) {}

std::string TokenService::Generate(int64_t player_id, const std::string& username) {
  auto now = std::chrono::system_clock::now();
  auto exp = now + expiry_;

  // Header
  std::string header = R"({"alg":"HS256","typ":"JWT"})";
  std::string header_b64 = Base64EncodeImpl(header);

  // Payload
  std::ostringstream payload_ss;
  payload_ss << R"({"sub":")" << player_id << R"(","username":")" << username << R"(","iat":)"
             << std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()
             << R"(,"exp":)"
             << std::chrono::duration_cast<std::chrono::seconds>(exp.time_since_epoch()).count()
             << R"(,"jti":")" << GenerateUUID() << "\"}";
  std::string payload_b64 = Base64EncodeImpl(payload_ss.str());

  // Signature
  std::string signature = ComputeHMAC(header_b64, payload_b64);
  std::string sig_b64 = Base64EncodeImpl(signature);

  return header_b64 + "." + payload_b64 + "." + sig_b64;
}

std::optional<int64_t> TokenService::Verify(const std::string& token) const {
  if (revocation_backend_ && revocation_backend_->IsRevoked(token)) return std::nullopt;
  // 检查黑名单
  {
    std::lock_guard<std::mutex> lock(blacklist_mutex_);
    if (blacklist_.count(token) > 0) return std::nullopt;
  }

  auto dot1 = token.find('.');
  auto dot2 = token.rfind('.');
  if (dot1 == std::string::npos || dot2 == std::string::npos || dot1 >= dot2) return std::nullopt;

  std::string header_b64 = token.substr(0, dot1);
  std::string payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
  std::string sig_b64 = token.substr(dot2 + 1);

  // 验证签名 — constant-time comparison to prevent timing side-channel.
  std::string expected_sig = ComputeHMAC(header_b64, payload_b64);
  std::string expected_b64 = Base64EncodeImpl(expected_sig);

  if (sig_b64.size() != expected_b64.size() ||
      CRYPTO_memcmp(sig_b64.data(), expected_b64.data(), sig_b64.size()) != 0) {
    return std::nullopt;
  }

  // 解析 payload 检查过期 — use proper JSON parser (nlohmann/json).
  std::string payload = Base64DecodeImpl(payload_b64);

  try {
    auto j = nlohmann::json::parse(payload);
    if (!j.contains("exp") || !j.contains("sub")) return std::nullopt;

    int64_t exp_val = 0;
    int64_t sub_val = 0;
    
    // Handle both integer and string representations
    if (j["exp"].is_string()) {
      exp_val = std::stoll(j["exp"].get<std::string>());
    } else {
      exp_val = j["exp"].get<int64_t>();
    }
    if (j["sub"].is_string()) {
      sub_val = std::stoll(j["sub"].get<std::string>());
    } else {
      sub_val = j["sub"].get<int64_t>();
    }

    auto now = std::chrono::system_clock::now();
    auto now_s = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    if (now_s > exp_val) return std::nullopt;

    // Check player-level revocation: if RevokeAll was called, any token
    // with iat < revoked_after is invalid (even if not in blacklist).
    if (j.contains("iat")) {
      int64_t iat_val = 0;
      if (j["iat"].is_string()) {
        iat_val = std::stoll(j["iat"].get<std::string>());
      } else {
        iat_val = j["iat"].get<int64_t>();
      }
      {
        std::lock_guard<std::mutex> lock(player_revoke_mutex_);
        auto it = player_revoked_after_.find(sub_val);
        if (it != player_revoked_after_.end() && iat_val < it->second) {
          return std::nullopt;
        }
      }
    }

    return sub_val;
  } catch (const nlohmann::json::exception&) {
    return std::nullopt;
  }
}

std::optional<std::string> TokenService::Refresh(const std::string& old_token) {
  auto player_id = Verify(old_token);
  if (!player_id.has_value()) return std::nullopt;

  // 撤销旧 token
  Revoke(old_token);

  // 生成新 token（需要从数据库获取 username）
  // 简化：使用 player_id 作为 username 前缀
  return Generate(player_id.value(), "player_" + std::to_string(player_id.value()));
}

void TokenService::Revoke(const std::string& token) {
  std::lock_guard<std::mutex> lock(blacklist_mutex_);
  blacklist_.insert(token);
  if (revocation_backend_) {
    revocation_backend_->Revoke(token, static_cast<int>(expiry_.count() * 3600));
  }
}

void TokenService::SetRevocationBackend(std::shared_ptr<TokenRevocationBackend> backend) {
  revocation_backend_ = std::move(backend);
}

void TokenService::RevokeAll(int64_t player_id) {
  auto now = std::chrono::system_clock::now();
  auto now_s = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  {
    std::lock_guard<std::mutex> lock(player_revoke_mutex_);
    player_revoked_after_[player_id] = now_s;
  }
  PE_LOG_INFO("RevokeAll for player {} — all tokens issued before {} are now invalid",
              player_id, now_s);
}

std::string TokenService::Base64Encode(const std::string& data) const {
  return Base64EncodeImpl(data);
}

std::string TokenService::Base64Decode(const std::string& data) const {
  return Base64DecodeImpl(data);
}

std::string TokenService::ComputeHMAC(const std::string& header_b64,
                                      const std::string& payload_b64) const {
  std::string data = header_b64 + "." + payload_b64;
  unsigned char hash[SHA256_DIGEST_LENGTH];

  HMAC(EVP_sha256(), secret_key_.data(), static_cast<int>(secret_key_.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash, nullptr);

  return std::string(reinterpret_cast<char*>(hash), SHA256_DIGEST_LENGTH);
}

std::string TokenService::GenerateUUID() {
  static std::atomic<uint64_t> counter{0};
  unsigned char buf[16];
  if (RAND_bytes(buf, sizeof(buf)) != 1) {
    // Fallback: use counter + time (emergency only)
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << counter++
        << std::setw(8) << static_cast<uint32_t>(now);
    return oss.str();
  }
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (int i = 0; i < 16; ++i) oss << std::setw(2) << static_cast<int>(buf[i]);
  return oss.str();
}

// ==================== AuthService ====================

AuthService::AuthService(std::string secret_key, phase14::IAccountRepository* account_repo)
    : token_service_(std::move(secret_key)), account_repo_(account_repo) {}

void AuthService::SetAccountRepository(phase14::IAccountRepository* repo) {
  account_repo_ = repo;
  // Pre-fill in-memory cache from DB
  if (account_repo_) {
    auto accounts = account_repo_->LoadAll();
    std::lock_guard<std::mutex> lock(players_mutex_);
    for (auto& ad : accounts) {
      PlayerAccount pa{};
      pa.id = ad.id;
      pa.username = ad.username;
      pa.display_name = ad.display_name;
      pa.password_hash = ad.password_hash;
      pa.chips = ad.chips;
      pa.total_profit = ad.total_profit;
      pa.hands_played = ad.hands_played;
      pa.elo_rating = ad.elo_rating;
      pa.avatar_url = ad.avatar_url;
      pa.role = static_cast<PlayerRole>(ad.role > 0 ? ad.role : 0);
      username_index_[pa.username] = pa;
      id_index_[pa.id] = pa;
      if (pa.id >= next_player_id_) next_player_id_ = pa.id + 1;
    }
    PE_LOG_INFO("Loaded {} accounts from database", accounts.size());
  }
}

void AuthService::EvictPlayer(int64_t player_id) {
  std::lock_guard<std::mutex> lock(players_mutex_);
  auto it = id_index_.find(player_id);
  if (it == id_index_.end()) return;
  std::string uname = it->second.username;
  username_index_.erase(uname);
  id_index_.erase(it);
  {
    std::lock_guard<std::mutex> l2(lockout_mutex_);
    failed_logins_.erase(uname);
  }
}

AuthResult AuthService::Register(const std::string& username, const std::string& password,
                                 const std::string& display_name) {
  AuthResult result;

  if (username.empty() || password.empty()) {
    result.success = false;
    result.error_message = "Username and password required";
    return result;
  }

  if (username.size() < 3 || username.size() > 32) {
    result.success = false;
    result.error_message = "Username must be 3-32 characters";
    return result;
  }

  if (password.size() < 6) {
    result.success = false;
    result.error_message = "Password must be at least 6 characters";
    return result;
  }

  {
    std::lock_guard<std::mutex> lock(players_mutex_);
    if (username_index_.count(username) > 0) {
      result.success = false;
      result.error_message = "Username already taken";
      return result;
    }
  }

  std::string salt = GenerateSalt();
  std::string hash = PBKDF2Hash(password, salt);
  // 存储格式: salt$hash
  std::string stored = salt + "$" + hash;

  PlayerAccount account;
  account.id = next_player_id_++;
  account.username = username;
  account.display_name = display_name.empty() ? username : display_name;
  account.password_hash = stored;
  account.chips = 1000;
  account.total_profit = 0;
  account.hands_played = 0;
  account.elo_rating = 1500;
  account.created_at = std::chrono::system_clock::now();
    account.last_login = std::chrono::system_clock::now();

  {
    std::lock_guard<std::mutex> lock(players_mutex_);
    username_index_[username] = account;
    id_index_[account.id] = account;
  }

  // Persist to database
  if (account_repo_) {
    phase14::AccountData ad{};
    ad.id = account.id;
    ad.username = account.username;
    ad.display_name = account.display_name;
    ad.password_hash = account.password_hash;
    ad.chips = account.chips;
    ad.total_profit = account.total_profit;
    ad.hands_played = account.hands_played;
    ad.elo_rating = account.elo_rating;
    ad.avatar_url = account.avatar_url;
    account_repo_->SaveAccount(ad);
    PE_LOG_INFO("Account persisted: {} (id={})", username, account.id);
  }

  PE_LOG_INFO("Player registered: {} (id={})", username, account.id);

  result.success = true;
  result.token = token_service_.Generate(account.id, username);
  result.player_id = account.id;
  return result;
}

AuthResult AuthService::Login(const std::string& username, const std::string& password) {
  AuthResult result;

  // 防爆破：先检查账户是否处于锁定中
  {
    std::lock_guard<std::mutex> lock(lockout_mutex_);
    auto it = failed_logins_.find(username);
    if (it != failed_logins_.end() &&
        std::chrono::steady_clock::now() < it->second.lock_until) {
      result.success = false;
      result.error_message = "Account temporarily locked due to too many failed logins";
      return result;
    }
  }

  PlayerAccount account;
  {
    std::lock_guard<std::mutex> lock(players_mutex_);
    auto it = username_index_.find(username);
    if (it == username_index_.end()) {
      result.success = false;
      result.error_message = "Invalid username or password";
      return result;
    }
    account = it->second;
  }

  // 解析存储的 salt 和 hash
  auto sep = account.password_hash.find('$');
  if (sep == std::string::npos) {
    result.success = false;
    result.error_message = "Corrupted password hash";
    return result;
  }

  std::string salt = account.password_hash.substr(0, sep);
  std::string computed = PBKDF2Hash(password, salt);
  std::string stored = account.password_hash.substr(sep + 1);

  // Constant-time comparison to prevent timing side-channel.
  if (computed.size() != stored.size() ||
      CRYPTO_memcmp(computed.data(), stored.data(), computed.size()) != 0) {
    // 失败计数：达到阈值则锁定账户 kLockoutMinutes 分钟
    std::lock_guard<std::mutex> lock(lockout_mutex_);
    auto& fl = failed_logins_[username];
    ++fl.attempts;
    if (fl.attempts >= kMaxLoginAttempts) {
      fl.lock_until =
          std::chrono::steady_clock::now() + std::chrono::minutes(kLockoutMinutes);
      PE_LOG_WARN("Account locked (brute-force guard): {} attempts for {}",
                   fl.attempts, username);
    }
    result.success = false;
    result.error_message = "Invalid username or password";
    return result;
  }

  // 登录成功：清零失败计数
  {
    std::lock_guard<std::mutex> lock(lockout_mutex_);
    failed_logins_.erase(username);
  }

  // 登录成功，更新最后登录时间
  {
    std::lock_guard<std::mutex> lock(players_mutex_);
    auto& acct = username_index_[username];
    acct.last_login = std::chrono::system_clock::now();
  }

  // Update login time in database
  if (account_repo_) {
    account_repo_->UpdateLastLogin(account.id);
  }

  result.success = true;
  result.token = token_service_.Generate(account.id, username);
  result.player_id = account.id;

  PE_LOG_INFO("Player logged in: {} (id={})", username, account.id);
  return result;
}

AuthResult AuthService::Authenticate(const std::string& token) {
  AuthResult result;

  auto player_id = token_service_.Verify(token);
  if (!player_id.has_value()) {
    result.success = false;
    result.error_message = "Invalid or expired token";
    return result;
  }

  auto account = GetPlayer(player_id.value());
  if (!account.has_value()) {
    result.success = false;
    result.error_message = "Player not found";
    return result;
  }

  result.success = true;
  result.player_id = player_id.value();
  result.token = token;  // token 仍然有效
  return result;
}

bool AuthService::Logout(const std::string& token) {
  token_service_.Revoke(token);
  PE_LOG_INFO("Player logged out (token revoked)");
  return true;
}

bool AuthService::UsernameExists(const std::string& username) const {
  std::lock_guard<std::mutex> lock(players_mutex_);
  return username_index_.count(username) > 0;
}


void AuthService::SetPlayerChips(int64_t player_id, int64_t chips) {
  std::lock_guard<std::mutex> lock(players_mutex_);
  auto it = id_index_.find(player_id);
  if (it == id_index_.end()) return;
  it->second.chips = chips;
  username_index_[it->second.username].chips = chips;
}

std::optional<PlayerAccount> AuthService::GetPlayer(int64_t player_id) const {
  std::lock_guard<std::mutex> lock(players_mutex_);
  auto it = id_index_.find(player_id);
  if (it != id_index_.end()) return it->second;
  return std::nullopt;
}

PlayerRole AuthService::GetPlayerRole(int64_t player_id) const {
  auto acct = GetPlayer(player_id);
  return acct.has_value() ? acct->role : PlayerRole::Player;
}

bool AuthService::HasRole(int64_t player_id, PlayerRole required) const {
  return GetPlayerRole(player_id) >= required;
}

void AuthService::SetPlayerRole(int64_t player_id, PlayerRole role) {
  std::lock_guard<std::mutex> lock(players_mutex_);
  auto it = id_index_.find(player_id);
  if (it == id_index_.end()) return;
  it->second.role = role;
  username_index_[it->second.username].role = role;
}

std::string AuthService::GenerateUUID() { return TokenService::GenerateUUID(); }

}  // namespace poker_engine::network
