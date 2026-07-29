#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "poker_engine/base/rate_limiter.h"
#include "poker_engine/base/result.h"
#include "poker_engine/security/crypto_utils.h"
#include "poker_engine/security/security_policy_engine.h"

namespace poker_engine::security {

// ==================== 安全路由中间件 ====================
// 验证请求签名、速率限制、IP 信誉检查、CSRF 防护

struct RoutePermission {
  std::string route;
  std::string method;         // GET, POST, etc.
  int required_auth_level;    // 0=public, 1=player, 2=admin
  int rate_limit_per_second;  // 每秒最大请求数
  bool require_https;         // 仅 HTTPS
  std::vector<std::string> allowed_roles;
};

struct VerifiedRequest {
  std::string path;
  std::string method;
  std::string client_ip;
  int64_t player_id;
  int auth_level;
  nlohmann::json body;
  nlohmann::json headers;
  std::chrono::system_clock::time_point received_at;
  bool is_internal;  // 来自内部服务
};

struct RouteHandler {
  std::string method;
  std::string path_pattern;  // 支持 /api/player/{id}/action
  std::function<base::Result<nlohmann::json>(const VerifiedRequest&)> handler;
  RoutePermission permission;
};

class SecureRouter {
 public:
  explicit SecureRouter(security::SecurityPolicyEngine* security_engine);

  // ========== 路由注册 ==========

  void AddRoute(RouteHandler handler);
  void AddRoutes(const std::vector<RouteHandler>& handlers);

  // ========== 请求处理 ==========

  // 处理 HTTP 请求（完整的请求验证流程）
  base::Result<nlohmann::json> HandleRequest(const std::string& method, const std::string& path,
                                             const std::string& client_ip,
                                             const nlohmann::json& headers,
                                             const nlohmann::json& body);

  // ========== 安全验证 ==========

  // 验证 JWT 令牌
  base::Result<int64_t> VerifyToken(const std::string& token);

  // 验证请求签名
  base::Result<void> VerifySignature(const std::string& payload, const std::string& signature,
                                     const std::string& timestamp);

  // CSRF 令牌验证
  base::Result<void> VerifyCSRF(const std::string& token, int64_t player_id);

  // 生成 CSRF 令牌
  std::string GenerateCSRFToken(int64_t player_id);

  // ========== 速率限制 ==========

  bool CheckRateLimit(const std::string& key, int per_second);
  bool CheckRateLimit(int64_t player_id, const std::string& route);

  // ========== 管理员操作审计 ==========

  void LogAdminAction(int64_t admin_id, const std::string& action, const nlohmann::json& details);

  // ========== 路由统计 ==========

  struct RouteStats {
    std::string path;
    int64_t total_requests;
    int64_t errors;
    int64_t avg_response_time_us;
    double error_rate;
  };

  std::vector<RouteStats> GetRouteStats() const;

  // ========== 白名单 ==========

  void AddWhitelistedIP(const std::string& ip);
  void RemoveWhitelistedIP(const std::string& ip);
  bool IsWhitelisted(const std::string& ip) const;

  void AddInternalIP(const std::string& ip);
  bool IsInternalIP(const std::string& ip) const;

 private:
  security::SecurityPolicyEngine* security_engine_;

  // 路由表
  std::vector<RouteHandler> routes_;
  mutable std::mutex routes_mutex_;

  // CSRF 令牌管理
  std::unordered_map<int64_t, std::pair<std::string, std::chrono::system_clock::time_point>>
      csrf_tokens_;
  std::mutex csrf_mutex_;

  // URI 参数模式
  static bool MatchRoutePattern(const std::string& pattern, const std::string& path,
                                std::unordered_map<std::string, std::string>& params);

  // 签名验证密钥
  std::string signing_key_;

  // 路由统计
  struct RouteStat {
    int64_t total = 0;
    int64_t errors = 0;
    std::chrono::microseconds total_time{0};
  };
  std::unordered_map<std::string, RouteStat> route_stats_;
  mutable std::mutex stats_mutex_;

  // IP 白名单
  std::unordered_set<std::string> whitelisted_ips_;
  std::unordered_set<std::string> internal_ips_;
  mutable std::mutex ip_mutex_;

  // 速率限制器
  std::unordered_map<std::string, std::unique_ptr<base::RateLimiter>> rate_limiters_;
  mutable std::mutex rate_mutex_;
};

}  // namespace poker_engine::security
