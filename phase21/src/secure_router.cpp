#include "poker_engine/security/secure_router.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::security {

SecureRouter::SecureRouter(security::SecurityPolicyEngine* security_engine)
    : security_engine_(security_engine) {}

void SecureRouter::AddRoute(RouteHandler handler) {
  std::lock_guard<std::mutex> lock(routes_mutex_);
  routes_.push_back(std::move(handler));
}

void SecureRouter::AddRoutes(const std::vector<RouteHandler>& handlers) {
  std::lock_guard<std::mutex> lock(routes_mutex_);
  for (auto& h : handlers) routes_.push_back(std::move(h));
}

base::Result<nlohmann::json> SecureRouter::HandleRequest(const std::string& method,
                                                         const std::string& path,
                                                         const std::string& client_ip,
                                                         const nlohmann::json& headers,
                                                         const nlohmann::json& body) {
  auto start = std::chrono::steady_clock::now();

  // ========== Step 1: IP 信誉检查 ==========
  if (!IsWhitelisted(client_ip)) {
    auto assessment = security_engine_->EvaluateConnection(client_ip, {}, nullptr);

    if (security_engine_->IsPlayerFlagged(std::stoll(assessment.player_id))) {
      auto decision = security_engine_->MakeDecision(assessment);
      if (decision.action >= SecurityPolicyEngine::ActionType::Ban) {
        return base::Result<nlohmann::json>::Err(
            base::MakeErrorCode(base::Error::PermissionDenied));
      }
    }
  }

  // ========== Step 2: 查找路由 ==========
  std::unordered_map<std::string, std::string> path_params;
  const RouteHandler* matched_route = nullptr;

  {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    for (auto& route : routes_) {
      if (route.method != method) continue;
      if (MatchRoutePattern(route.path_pattern, path, path_params)) {
        matched_route = &route;
        break;
      }
    }
  }

  if (!matched_route) {
    return base::Result<nlohmann::json>::Err(base::MakeErrorCode(base::Error::NotFound));
  }

  // ========== Step 3: 身份验证 ==========
  int64_t player_id = -1;
  if (matched_route->permission.required_auth_level > 0) {
    std::string token;
    if (headers.contains("Authorization")) {
      token = headers["Authorization"].get<std::string>();
      if (token.substr(0, 7) == "Bearer ") token = token.substr(7);
    }

    if (token.empty()) {
      return base::Result<nlohmann::json>::Err(
          base::MakeErrorCode(base::Error::AuthenticationFailed));
    }

    auto verify_result = VerifyToken(token);
    if (!verify_result.IsOk()) {
      return base::Result<nlohmann::json>::Err(verify_result.Error());
    }
    player_id = verify_result.Unwrap();
  }

  // ========== Step 4: 速率限制 ==========
  std::string rate_limit_key = std::to_string(player_id) + ":" + matched_route->path_pattern;

  if (!CheckRateLimit(rate_limit_key, matched_route->permission.rate_limit_per_second)) {
    return base::Result<nlohmann::json>::Err(base::MakeErrorCode(base::Error::RateLimited));
  }

  // ========== Step 5: CSRF 验证 (仅 POST/PUT/DELETE) ==========
  if (method == "POST" || method == "PUT" || method == "DELETE") {
    if (headers.contains("X-CSRF-Token") && player_id >= 0) {
      auto csrf_result = VerifyCSRF(headers["X-CSRF-Token"].get<std::string>(), player_id);
      if (!csrf_result.IsOk()) {
        return base::Result<nlohmann::json>::Err(
            base::MakeErrorCode(base::Error::PermissionDenied));
      }
    }
  }

  // ========== Step 6: 构建已验证请求 ==========
  VerifiedRequest req;
  req.path = path;
  req.method = method;
  req.client_ip = client_ip;
  req.player_id = player_id;
  req.auth_level = matched_route->permission.required_auth_level;
  req.body = body;
  req.headers = headers;
  req.received_at = std::chrono::system_clock::now();
  req.is_internal = IsInternalIP(client_ip);
  req.path = path;

  for (auto& [key, value] : path_params) {
    req.body["_params"][key] = value;
  }

  // ========== Step 7: 调用处理函数 ==========
  auto result = matched_route->handler(req);

  // 记录统计
  auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);

  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto& stat = route_stats_[matched_route->path_pattern];
    stat.total++;
    if (!result.IsOk()) stat.errors++;
    stat.total_time += elapsed;
  }

  // 审计日志
  if (matched_route->permission.required_auth_level >= 2) {
    LogAdminAction(
        player_id, matched_route->path_pattern,
        result.IsOk() ? nlohmann::json{} : nlohmann::json{{"error", result.Error().message()}});
  }

  return result;
}

// ==================== 路由匹配 ====================

bool SecureRouter::MatchRoutePattern(const std::string& pattern, const std::string& path,
                                     std::unordered_map<std::string, std::string>& params) {
  std::istringstream p_stream(pattern);
  std::istringstream path_stream(path);
  std::string p_seg, path_seg;

  while (std::getline(p_stream, p_seg, '/') && std::getline(path_stream, path_seg, '/')) {
    if (p_seg.empty() && path_seg.empty()) continue;

    if (p_seg.size() > 0 && p_seg[0] == '{') {
      // 参数段: {param_name} 或 {param_name:\d+}
      size_t colon = p_seg.find(':');
      std::string param_name = p_seg.substr(1, colon - 1);
      params[param_name] = path_seg;
    } else if (p_seg != path_seg) {
      return false;
    }
  }

  // 确保两者都消费完毕
  return !std::getline(p_stream, p_seg, '/') && !std::getline(path_stream, path_seg, '/');
}

// ==================== JWT 验证（简化）====================

base::Result<int64_t> SecureRouter::VerifyToken(const std::string& token) {
  if (token.empty()) {
    return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::AuthenticationFailed));
  }

  // 简化: 解码 JWT 载荷
  // 实际应验证签名和有效期
  try {
    size_t dot1 = token.find('.');
    if (dot1 == std::string::npos) {
      return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::AuthenticationFailed));
    }

    // 解码 payload (简化)
    std::string payload = token.substr(dot1 + 1);
    // 实际: Base64URL 解码 + JSON 解析

    return base::Result<int64_t>::Ok(1);  // 简化
  } catch (...) {
    return base::Result<int64_t>::Err(base::MakeErrorCode(base::Error::AuthenticationFailed));
  }
}

// ==================== 请求签名验证 ====================

base::Result<void> SecureRouter::VerifySignature(const std::string& payload,
                                                 const std::string& signature,
                                                 const std::string& timestamp) {
  // 检查时间戳有效性 (5 分钟窗口)
  try {
    int64_t ts = std::stoll(timestamp);
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();

    if (std::abs(now - ts) > 300) {
      return base::Result<void>::Err(base::MakeErrorCode(base::Error::Timeout));
    }
  } catch (...) {
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::InvalidArgument));
  }

  // 计算预期签名
  std::string expected = CryptoUtils::SHA256Hex(payload + "|" + timestamp + "|" + signing_key_);

  if (!CryptoUtils::VerifyHMAC(std::vector<uint8_t>(signing_key_.begin(), signing_key_.end()),
                               std::vector<uint8_t>(payload.begin(), payload.end()),
                               std::vector<uint8_t>(expected.begin(), expected.end()))) {
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::AuthenticationFailed));
  }

  return base::Result<void>::Ok();
}

// ==================== CSRF 令牌 ====================

std::string SecureRouter::GenerateCSRFToken(int64_t player_id) {
  std::lock_guard<std::mutex> lock(csrf_mutex_);

  std::string token = CryptoUtils::GenerateToken();
  csrf_tokens_[player_id] = {CryptoUtils::HashToken(token),
                             std::chrono::system_clock::now() + std::chrono::hours(2)};

  return token;
}

base::Result<void> SecureRouter::VerifyCSRF(const std::string& token, int64_t player_id) {
  std::lock_guard<std::mutex> lock(csrf_mutex_);

  auto it = csrf_tokens_.find(player_id);
  if (it == csrf_tokens_.end()) {
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::PermissionDenied));
  }

  auto& [hashed, expiry] = it->second;

  // 检查过期
  if (std::chrono::system_clock::now() > expiry) {
    csrf_tokens_.erase(it);
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::Timeout));
  }

  // 验证令牌
  if (CryptoUtils::HashToken(token) != hashed) {
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::PermissionDenied));
  }

  // 单次使用，删除令牌
  csrf_tokens_.erase(it);
  return base::Result<void>::Ok();
}

// ==================== 速率限制 ====================

bool SecureRouter::CheckRateLimit(const std::string& key, int per_second) {
  std::lock_guard<std::mutex> lock(rate_mutex_);

  if (!rate_limiters_.count(key)) {
    rate_limiters_[key] =
        std::make_unique<base::RateLimiter>(base::RateLimiter::Config::PerSecond(per_second));
  }

  return rate_limiters_[key]->TryConsume(1);
}

bool SecureRouter::CheckRateLimit(int64_t player_id, const std::string& route) {
  return CheckRateLimit(std::to_string(player_id) + ":" + route, 5);
}

// ==================== 日志与管理 ============

void SecureRouter::LogAdminAction(int64_t admin_id, const std::string& action,
                                  const nlohmann::json& details) {
  // Admin action audit log (simplified — no AUDIT_LOG macro available)
  PE_LOG_INFO("Admin action: admin_id={}, action={}, details={}", admin_id, action, details.dump());
}

std::vector<SecureRouter::RouteStats> SecureRouter::GetRouteStats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  std::vector<RouteStats> result;
  for (auto& [path, stat] : route_stats_) {
    RouteStats rs;
    rs.path = path;
    rs.total_requests = stat.total;
    rs.errors = stat.errors;
    rs.avg_response_time_us =
        stat.total > 0 ? static_cast<int64_t>(stat.total_time.count() / stat.total) : 0;
    rs.error_rate = stat.total > 0 ? static_cast<double>(stat.errors) / stat.total : 0.0;
    result.push_back(rs);
  }
  return result;
}

void SecureRouter::AddWhitelistedIP(const std::string& ip) {
  std::lock_guard<std::mutex> lock(ip_mutex_);
  whitelisted_ips_.insert(ip);
}

void SecureRouter::RemoveWhitelistedIP(const std::string& ip) {
  std::lock_guard<std::mutex> lock(ip_mutex_);
  whitelisted_ips_.erase(ip);
}

bool SecureRouter::IsWhitelisted(const std::string& ip) const {
  std::lock_guard<std::mutex> lock(ip_mutex_);
  return whitelisted_ips_.count(ip) > 0;
}

void SecureRouter::AddInternalIP(const std::string& ip) {
  std::lock_guard<std::mutex> lock(ip_mutex_);
  internal_ips_.insert(ip);
}

bool SecureRouter::IsInternalIP(const std::string& ip) const {
  std::lock_guard<std::mutex> lock(ip_mutex_);
  return internal_ips_.count(ip) > 0;
}

}  // namespace poker_engine::security
