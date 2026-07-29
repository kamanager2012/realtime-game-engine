#pragma once

#include <functional>
#include <optional>
#include <string>

#include "poker_engine/network/auth_service.h"

namespace poker_engine::network {

// ==================== WS 连接认证中间件 ====================
// 在 WS onConnection 时检查 token
// 在每个 WS onMessage 时验证授权

struct AuthContext {
  bool authenticated = false;
  int64_t player_id = -1;
  std::string username;
  std::string token;
  std::string table_id;
  std::string error_message;
};

class AuthMiddleware {
 public:
  explicit AuthMiddleware(AuthService& auth_service) : auth_service_(auth_service) {}

  // 从 WS 连接中提取 token 并验证
  // URL 格式: ws://host:port/?token=xxx&table_id=yyy
  AuthContext AuthenticateConnection(const std::string& query_string) {
    AuthContext ctx;

    std::string token = ExtractParam(query_string, "token");
    std::string table_id = ExtractParam(query_string, "table_id");

    if (token.empty()) {
      ctx.error_message = "Missing token";
      return ctx;
    }

    auto auth_result = auth_service_.Authenticate(token);
    if (!auth_result.success) {
      ctx.error_message = auth_result.error_message;
      return ctx;
    }

    ctx.authenticated = true;
    ctx.player_id = auth_result.player_id;
    ctx.token = token;
    ctx.table_id = table_id;

    auto account = auth_service_.GetPlayer(ctx.player_id);
    if (account.has_value()) {
      ctx.username = account->username;
    }

    return ctx;
  }

  // 检查玩家是否有权访问指定牌桌
  bool AuthorizeTable(int64_t player_id, const std::string& table_id) const {
    // 在 MVP 阶段：任何已认证玩家可以加入任何牌桌
    // 生产环境：检查牌桌密码、VIP 权限等
    return !table_id.empty() && player_id > 0;
  }

 private:
  AuthService& auth_service_;

  static std::string ExtractParam(const std::string& query, const std::string& key) {
    auto pos = query.find(key + "=");
    if (pos == std::string::npos) return "";
    pos += key.size() + 1;  // skip "key="
    auto end = query.find('&', pos);
    if (end == std::string::npos) end = query.size();
    return query.substr(pos, end - pos);
  }
};

}  // namespace poker_engine::network
