// HttpRouter - extracted HTTP routing module.
// Transitional shim: functions mirror poker_ws_server.cpp.
// Future: poker_ws_server calls HandleHttpRequest() and removes duplicates.

#include "http_router.h"

#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <unordered_set>

namespace poker_engine::cli {

namespace {
static std::unordered_set<std::string> g_allowed_origins;
}

void InitAllowedOrigins() {
  if (const char* v = std::getenv("POKER_ALLOWED_ORIGINS")) {
    std::string s = v;
    size_t start = 0;
    while (start < s.size()) {
      auto comma = s.find(',', start);
      if (comma == std::string::npos) comma = s.size();
      std::string origin = s.substr(start, comma - start);
      if (!origin.empty()) g_allowed_origins.insert(origin);
      start = comma + 1;
    }
  }
  g_allowed_origins.insert("http://localhost:5173");
  g_allowed_origins.insert("http://localhost:3000");
  g_allowed_origins.insert("http://127.0.0.1:5173");
}

bool IsOriginAllowed(const std::string& origin) {
  return g_allowed_origins.count(origin) > 0;
}

std::string ExtractOriginHeader(const std::string& req) {
  auto header_end = req.find("\r\n\r\n");
  if (header_end == std::string::npos) return "";
  std::string headers = req.substr(0, header_end);
  auto pos = headers.find("Origin:");
  if (pos == std::string::npos) pos = headers.find("origin:");
  if (pos == std::string::npos) return "";
  pos += 7;
  while (pos < headers.size() && headers[pos] == ' ') ++pos;
  auto end = headers.find("\r\n", pos);
  if (end == std::string::npos) end = headers.size();
  return headers.substr(pos, end - pos);
}

std::string BuildHttpResponse(int code, const std::string& body,
                               const std::string& request_origin) {
  std::ostringstream r;
  r << "HTTP/1.1 " << code << " " << (code == 200 ? "OK" : "Error") << "\r\n";
  r << "Content-Type: application/json\r\n";
  r << "Content-Length: " << body.size() << "\r\n";
  r << "Access-Control-Allow-Origin: " 
    << (request_origin.empty() ? "http://localhost:5173" : request_origin) << "\r\n";
  r << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
  r << "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n";
  r << "X-Content-Type-Options: nosniff\r\n";
  r << "X-Frame-Options: DENY\r\n";
  r << "Connection: close\r\n\r\n";
  r << body;
  return r.str();
}

std::string ParseHttpBody(const std::string& req) {
  auto pos = req.find("\r\n\r\n");
  if (pos == std::string::npos) return "";
  return req.substr(pos + 4);
}

std::string ExtractBearerToken(const std::string& headers) {
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

bool IsAdminRequest(const std::string& req, const std::string& admin_token) {
  if (admin_token.empty()) return false;
  std::string token = ExtractBearerToken(req);
  return !token.empty() && token == admin_token;
}

bool HandleHttpRequest(int fd, const std::string& data,
                       const ServerContext& ctx) {
  (void)fd; (void)data; (void)ctx;
  return false;
}

}  // namespace poker_engine::cli
