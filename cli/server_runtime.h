#pragma once

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <openssl/rand.h>
#include <sstream>
#include <string>

#include "poker_engine/network/auth_service.h"

namespace poker_engine::cli {

struct ServerRuntimeConfig {
  std::string db_path = "poker_data.db";
  std::string jwt_secret;
  std::string admin_token;
  bool production_mode = false;
  std::string redis_url;
  std::string postgres_url;
  std::string instance_id = "node-1";
};

namespace {
std::string GenerateDevSecret() {
  unsigned char buf[32];
  if (RAND_bytes(buf, sizeof(buf)) != 1) {
    // Last resort: refuse to start without entropy.
    std::cerr << "[FATAL] Cannot generate dev secret: no entropy available\n";
    std::exit(1);
  }
  std::ostringstream oss;
  oss << "dev-";
  for (int i = 0; i < 32; ++i)
    oss << "0123456789abcdef"[buf[i] & 0xf];
  return oss.str();
}
}  // namespace

inline ServerRuntimeConfig LoadServerRuntimeConfig() {
  ServerRuntimeConfig cfg;
  if (const char* v = std::getenv("POKER_PRODUCTION")) {
    cfg.production_mode = (std::string(v) == "1" || std::string(v) == "true");
  }
  if (const char* v = std::getenv("POKER_DB_PATH")) {
    cfg.db_path = v;
  } else if (const char* v = std::getenv("DB_PATH")) {
    cfg.db_path = v;
  }
  if (const char* v = std::getenv("POKER_JWT_SECRET")) {
    cfg.jwt_secret = v;
  } else if (const char* v = std::getenv("JWT_SECRET")) {
    cfg.jwt_secret = v;
  }
  if (const char* v = std::getenv("POKER_ADMIN_TOKEN")) {
    cfg.admin_token = v;
  }
  if (const char* v = std::getenv("POKER_REDIS_URL")) {
    cfg.redis_url = v;
  } else if (const char* v = std::getenv("REDIS_URL")) {
    cfg.redis_url = v;
  }
  if (const char* v = std::getenv("POKER_POSTGRES_URL")) {
    cfg.postgres_url = v;
  } else if (const char* v = std::getenv("DATABASE_URL")) {
    cfg.postgres_url = v;
  }
  if (const char* v = std::getenv("POKER_INSTANCE_ID")) {
    cfg.instance_id = v;
  }

  if (cfg.production_mode) {
    if (cfg.jwt_secret.empty() || cfg.admin_token.empty()) {
      std::cerr << "[FATAL] Production mode requires POKER_JWT_SECRET and POKER_ADMIN_TOKEN\n";
      std::exit(1);
    }
  } else {
    if (cfg.jwt_secret.empty()) {
      cfg.jwt_secret = GenerateDevSecret();
      std::cout << "[DEV] Auto-generated JWT secret: " << cfg.jwt_secret << "\n";
    }
    if (cfg.admin_token.empty()) {
      cfg.admin_token = GenerateDevSecret();
      std::cout << "[DEV] Auto-generated admin token: " << cfg.admin_token << "\n";
    }
  }
  return cfg;
}

inline std::string ExtractBearerToken(const std::string& headers) {
  const std::string prefix = "Authorization: Bearer ";
  auto pos = headers.find(prefix);
  if (pos == std::string::npos) {
    pos = headers.find("authorization: Bearer ");
    if (pos == std::string::npos) return "";
    pos += 22;
  } else {
    pos += prefix.size();
  }
  auto end = headers.find("\r\n", pos);
  if (end == std::string::npos) end = headers.size();
  return headers.substr(pos, end - pos);
}

inline bool VerifyAuthToken(poker_engine::network::AuthService* auth, const std::string& token,
                            int64_t& player_id) {
  if (!auth || token.empty()) return false;
  auto verified = auth->GetTokenService().Verify(token);
  if (!verified.has_value()) return false;
  player_id = verified.value();
  return true;
}

// Extract the auth token carried as the WebSocket subprotocol
// (Sec-WebSocket-Protocol header). Used as a browser-safe alternative to the
// Authorization header, which browsers cannot set on a WebSocket connection.
// The token is never read from the URL query string.
inline std::string ExtractWsProtocolToken(const std::string& headers) {
  const std::string prefix = "Sec-WebSocket-Protocol: ";
  auto pos = headers.find(prefix);
  if (pos == std::string::npos) {
    pos = headers.find("sec-websocket-protocol: ");
    if (pos == std::string::npos) return "";
    pos += 24;
  } else {
    pos += prefix.size();
  }
  auto end = headers.find("\r\n", pos);
  if (end == std::string::npos) end = headers.size();
  std::string proto = headers.substr(pos, end - pos);
  // If multiple subprotocols were offered (comma-separated), take the first.
  auto comma = proto.find(',');
  if (comma != std::string::npos) proto = proto.substr(0, comma);
  // Trim surrounding whitespace.
  auto s = proto.find_first_not_of(" \t");
  auto e = proto.find_last_not_of(" \t");
  if (s == std::string::npos) return "";
  return proto.substr(s, e - s + 1);
}

}  // namespace poker_engine::cli
