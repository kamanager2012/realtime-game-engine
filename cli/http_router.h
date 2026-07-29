#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <unordered_set>

// Forward declarations
namespace poker_engine::network {
class AuthService;
class GameServer;
}
namespace poker_engine::tournament {
class TournamentServer;
}

// Extracted HTTP routing from poker_ws_server monolith.
namespace poker_engine::cli {

// Server context passed to handlers.
struct ServerContext {
  poker_engine::network::AuthService* auth = nullptr;
  poker_engine::network::GameServer* game = nullptr;
  poker_engine::tournament::TournamentServer* tournament = nullptr;
  bool db_initialized = false;
  bool production_mode = false;
  std::string admin_token;
  std::string instance_id;
  std::atomic<bool>* redis_connected = nullptr;
};

// HTTP response builder.
std::string BuildHttpResponse(int code, const std::string& body,
                               const std::string& request_origin = "");

// Parse HTTP body from raw request.
std::string ParseHttpBody(const std::string& req);

// Extract Authorization: Bearer token.
std::string ExtractBearerToken(const std::string& headers);

// Extract Origin header for CORS.
std::string ExtractOriginHeader(const std::string& req);

// Check if origin is allowed (CORS). Call InitAllowedOrigins() first.
bool IsOriginAllowed(const std::string& origin);
void InitAllowedOrigins();

// Check if request carries valid admin token.
bool IsAdminRequest(const std::string& req, const std::string& admin_token);

// Main HTTP handler: routes GET/POST requests and writes response to fd.
// Returns true if the request was handled as HTTP (caller should close fd).
// Returns false if not an HTTP request (caller should try WS/game handler).
bool HandleHttpRequest(int fd, const std::string& data,
                       const ServerContext& ctx);

}  // namespace poker_engine::cli
