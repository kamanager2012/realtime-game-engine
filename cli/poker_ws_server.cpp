#include <cmath>
#include <fstream>
#include <netinet/in.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <vector>
#include <csignal>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <chrono>
#include <ctime>

#include "poker_engine/game/table.h"
#include "poker_engine/game/showdown_evaluator.h"
#include "poker_engine/network/auth_service.h"
#include "poker_engine/network/game_server.h"
#include "poker_engine/phase14/database_manager.h"
#include "poker_engine/phase14/player_repository.h"
#include "poker_engine/phase14/stat_repository.h"
#include "poker_engine/tournament/tournament.h"
#include "poker_engine/tournament/tournament_server.h"
#include "poker_engine/network/cfr_policy_store.h"
#include "poker_engine/base/rate_limiter.h"
#include "server_runtime.h"
#include "buy_in_policy.h"
#include "poker_engine/network/health_probe.h"
#include "poker_engine/network/metrics_registry.h"
#include "poker_engine/network/distributed_session_store.h"
#include "poker_engine/replay/replay_types.h"
#include "poker_engine/anticheat/anticheat.h"
#include "poker_engine/anticheat/ml_engine.h"
#include "poker_engine/base/structured_audit_logger.h"
#include <arpa/inet.h>

using namespace poker_engine::network;
using namespace poker_engine::phase14;
using namespace poker_engine::tournament;

static volatile bool g_running = true;
static GameServer* g_game = nullptr;
static AuthService* g_auth = nullptr;
static bool g_db_initialized = false;
static TournamentServer* g_tourn = nullptr;
static std::string g_admin_token;
static bool g_production_mode = false;
static poker_engine::network::DistributedSessionStorePtr g_session_store;
static std::string g_instance_id;
static std::atomic<bool> g_redis_connected{false};
static std::thread g_redis_heartbeat_thread;

// Anti-cheat: real-time collusion / bot detection on every completed hand.
static poker_engine::anticheat::AntiCheatManager g_anticheat;
static int64_t g_anticheat_hand_counter = 0;

// Banned players (anti-cheat enforcement).
static std::unordered_set<int64_t> g_banned_players;
static std::mutex g_ban_mutex;

// Live WebSocket client registry (exposed at file scope so the anti-cheat
// path can force-close a banned player's open connections).
struct WsClient {
  int fd;
  int32_t player_id = 0;
  std::string name;
  std::string table_id;
  std::string token;
  bool is_spectator = false;
};
static std::unordered_map<int, WsClient> g_ws_clients;
static std::recursive_mutex g_ws_clients_mutex;

// Force-close every open WebSocket belonging to `pid` (used when a player is
// permanently banned, so the ban takes effect immediately rather than only on
// their next reconnect).
static void KickPlayerConnections(int32_t pid) {
  std::lock_guard<std::recursive_mutex> lock(g_ws_clients_mutex);
  for (auto it = g_ws_clients.begin(); it != g_ws_clients.end();) {
    if (it->second.player_id == pid) {
      int fd = it->first;
      // Minimal WebSocket close frame (FIN + close opcode, no payload).
      const char close_frame[2] = {static_cast<char>(0x88), 0x00};
      send(fd, close_frame, 2, 0);
      close(fd);
      it = g_ws_clients.erase(it);
    } else {
      ++it;
    }
  }
}

static void SaveBans() {
  std::lock_guard<std::mutex> lock(g_ban_mutex);
  std::ofstream f("bans.json");
  f << "[";
  bool first = true;
  for (auto id : g_banned_players) {
    if (!first) f << ","; first = false;
    f << id;
  }
  f << "]";
}
static void LoadBans() {
  std::ifstream f("bans.json");
  if (!f.is_open()) return;
  std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::lock_guard<std::mutex> lock(g_ban_mutex);
  size_t pos = 0;
  while ((pos = content.find_first_of("0123456789", pos)) != std::string::npos) {
    int64_t id = std::atoll(content.c_str() + pos);
    if (id > 0) g_banned_players.insert(id);
    pos = content.find(',', pos);
    if (pos == std::string::npos) break;
  }
  if (!g_banned_players.empty())
    std::cout << "[Server] Loaded " << g_banned_players.size() << " banned players\n";
}

// Track which tables each player is on (for anti-cheat kick).
struct PlayerTableEntry { std::string table_id; int32_t player_id; };
static std::vector<PlayerTableEntry> g_player_tables;
static std::mutex g_player_tables_mutex;
static void TrackPlayerTable(int32_t pid, const std::string& tid) {
  std::lock_guard<std::mutex> lock(g_player_tables_mutex);
  g_player_tables.push_back({tid, pid});
}
static void UntrackPlayerTable(int32_t pid, const std::string& tid) {
  std::lock_guard<std::mutex> lock(g_player_tables_mutex);
  g_player_tables.erase(
      std::remove_if(g_player_tables.begin(), g_player_tables.end(),
                     [&](const auto& e) { return e.player_id == pid && e.table_id == tid; }),
      g_player_tables.end());
}

// Achievement tracking
static std::unordered_map<int64_t, int64_t> g_win_streak;

// Forward declaration; defined near the server bootstrap.
static void OnAnticheatAlert(const poker_engine::anticheat::CheatAlert& alert);


// ==================== Matchmaking Queue ====================
struct MatchEntry {
  int64_t player_id;
  std::string name;
  std::string game_type;  // "nlhe" or "plo"
  double buy_in;
  std::chrono::steady_clock::time_point joined_at;
};
static std::vector<MatchEntry> g_match_queue;
static std::mutex g_match_mutex;
static int g_match_table_counter = 100;
static int g_max_clients = 500;           // env: POKER_MAX_CLIENTS
static int g_max_tables = 100;            // env: POKER_MAX_TABLES
static const int MATCH_TABLE_SIZE = 6;

// Pending buy-ins for crash recovery.
// When a debit succeeds but the join hasn't completed, we record the entry
// so that startup recovery can refund any orphaned funds.
struct PendingBuyIn {
  int64_t player_id;
  int64_t amount;
  std::string table_id;
  PendingBuyIn(int64_t pid, int64_t amt, std::string tid)
      : player_id(pid), amount(amt), table_id(std::move(tid)) {}
};
static std::vector<PendingBuyIn> g_pending_buyins;
static std::mutex g_pending_mutex;

static bool refundBuyIn(int64_t player_id, int64_t amount, const std::string& table_id,
                        std::string& error);

// Refund any orphaned buy-ins from a previous crash.
static void RecoverOrphanedBuyIns() {
  std::lock_guard<std::mutex> lock(g_pending_mutex);
  if (g_pending_buyins.empty()) return;
  std::cout << "[Startup] Recovering " << g_pending_buyins.size() << " orphaned buy-ins\n";
  for (const auto& p : g_pending_buyins) {
    std::string err;
    if (!refundBuyIn(p.player_id, p.amount, p.table_id + ":crash_recovery", err)) {
      std::cerr << "[CRITICAL] Failed to refund orphaned buy-in pid=" << p.player_id
                << " amount=" << p.amount << " err=" << err << "\n";
    } else {
      std::cout << "[Recovery] Refunded " << p.amount << " chips to player " << p.player_id << "\n";
    }
  }
  g_pending_buyins.clear();
}


static bool debitBuyIn(int64_t player_id, int64_t amount, const std::string& table_id,
                       std::string& error);
static bool refundBuyIn(int64_t player_id, int64_t amount, const std::string& table_id,
                        std::string& error);

static bool seatPlayerWithBuyIn(int32_t player_id, const std::string& name, const std::string& table_id,
                                int seat_index, int64_t buy_in, const std::string& token);

static std::string JoinMatchmaking(int64_t player_id, const std::string& name,
                                   const std::string& game_type, double buy_in) {
  std::lock_guard<std::mutex> lock(g_match_mutex);
  // Check if already in queue
  for (auto& e : g_match_queue) {
    if (e.player_id == player_id) return "{\"result\":\"already_in_queue\"}";
  }
  MatchEntry entry{player_id, name, game_type, buy_in, std::chrono::steady_clock::now()};
  g_match_queue.push_back(entry);

  // Check if we have enough players
  // Group by game_type + buy_in (within 50% range)
  std::vector<size_t> matched;
  for (size_t i = 0; i < g_match_queue.size(); i++) {
    if (g_match_queue[i].game_type == game_type &&
        std::abs(g_match_queue[i].buy_in - buy_in) <= buy_in * 0.5) {
      matched.push_back(i);
    }
  }

  if (static_cast<int>(matched.size()) >= MATCH_TABLE_SIZE) {
    // Check table limit before creating new table.
    if (g_game && g_game->TableCount() >= g_max_tables) {
      return "{\"result\":\"error\",\"message\":\"server at capacity\"}";
    }
    // Create a new table and add matched players
    std::string table_name = "match_" + std::to_string(++g_match_table_counter);
    double sb = buy_in / 100.0;
    double bb = sb * 2;
    std::string table_id = g_game->CreateTable(table_name, MATCH_TABLE_SIZE, sb, bb);
    if (!table_id.empty()) {
      // Remove matched players from queue (reverse order to preserve indices)
      for (int j = static_cast<int>(matched.size()) - 1; j >= 0; --j) {
        auto& e = g_match_queue[matched[j]];
        seatPlayerWithBuyIn(static_cast<int32_t>(e.player_id), e.name, table_id, -1, static_cast<int64_t>(buy_in), "");
        g_match_queue.erase(g_match_queue.begin() + matched[j]);
      }
      // Fill remaining seats with bots
      g_game->AddBots(table_id, MATCH_TABLE_SIZE - static_cast<int>(matched.size()), buy_in);
      return "{\"result\":\"matched\",\"table_id\":\"" + table_id + "\"}";
    }
  }

  int position = 0;
  for (auto& e : g_match_queue) {
    if (e.game_type == game_type) position++;
  }
  return "{\"result\":\"waiting\",\"position\":" + std::to_string(position) +
         ",\"queue_size\":" + std::to_string(g_match_queue.size()) + "}";
}

static std::string LeaveMatchmaking(int64_t player_id) {
  std::lock_guard<std::mutex> lock(g_match_mutex);
  for (auto it = g_match_queue.begin(); it != g_match_queue.end(); ++it) {
    if (it->player_id == player_id) {
      g_match_queue.erase(it);
      return "{\"result\":\"left\"}";
    }
  }
  return "{\"result\":\"not_in_queue\"}";
}

static std::string GetMatchmakingStatus(int64_t player_id) {
  std::lock_guard<std::mutex> lock(g_match_mutex);
  for (auto& e : g_match_queue) {
    if (e.player_id == player_id) {
      int position = 0;
      for (auto& q : g_match_queue) {
        if (q.game_type == e.game_type) position++;
      }
      return "{\"in_queue\":true,\"game_type\":\"" + e.game_type +
             "\",\"buy_in\":" + std::to_string(e.buy_in) +
             ",\"position\":" + std::to_string(position) + "}";
    }
  }
  return "{\"in_queue\":false}";
}

void SignalHandler(int) { g_running = false; }

// ========== WebSocket helpers ==========

static std::string wsDecode(const std::string& data) {
  constexpr size_t kMaxFramePayload = 64 * 1024;  // 64KB limit
  if (data.size() < 2) return "";
  uint8_t opcode = data[0] & 0x0F;
  if (opcode == 0x8) return "";      // close
  if (opcode == 0x9) return "PING";  // ping
  bool masked = (data[1] & 0x80) != 0;
  size_t len = data[1] & 0x7F;
  size_t pos = 2;
  if (len == 126) {
    if (data.size() < 4) return "";
    len = ((uint8_t)data[2] << 8) | (uint8_t)data[3];
    pos = 4;
  } else if (len == 127) {
    return "";  // 64-bit frames rejected
  }
  if (len > kMaxFramePayload) return "";  // oversized frame
  char mask[4] = {};
  if (masked) {
    if (data.size() < pos + 4) return "";
    memcpy(mask, &data[pos], 4);
    pos += 4;
  }
  if (data.size() < pos + len) return "";
  std::string result(data.begin() + pos, data.begin() + pos + len);
  if (masked)
    for (size_t i = 0; i < result.size(); ++i) result[i] ^= mask[i % 4];
  return result;
}

static std::string wsEncode(const std::string& msg) {
  std::string frame;
  frame += (char)0x81;
  if (msg.size() < 126) {
    frame += (char)msg.size();
  } else if (msg.size() < 65536) {
    frame += (char)126;
    frame += (char)(msg.size() >> 8);
    frame += (char)(msg.size() & 0xFF);
  }
  frame += msg;
  return frame;
}

static std::string wsPong() {
  std::string frame;
  frame += (char)0x8A;  // pong
  frame += (char)0;
  return frame;
}

static std::string sha1b64(const std::string& input) {
  unsigned char digest[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
  BIO* b64 = BIO_new(BIO_f_base64());
  BIO* mem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, mem);
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(b64, digest, SHA_DIGEST_LENGTH);
  BIO_flush(b64);
  BUF_MEM* buf = nullptr;
  BIO_get_mem_ptr(b64, &buf);
  std::string result(buf->data, buf->length);
  BIO_free_all(b64);
  return result;
}

static std::string wsHandshake(const std::string& req) {
  auto pos = req.find("Sec-WebSocket-Key: ");
  if (pos == std::string::npos) return "";
  pos += 19;
  auto end = req.find("\r\n", pos);
  std::string key = req.substr(pos, end - pos) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  std::string accept = sha1b64(key);
  std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: "
         "Upgrade\r\nSec-WebSocket-Accept: " +
         accept;
  // Echo the negotiated subprotocol so the browser accepts the connection.
  // The auth token is carried as the subprotocol value (browser-safe: it is
  // never placed in the URL query string, so it cannot leak into proxy/edge
  // access logs or browser history).
  auto ppos = req.find("Sec-WebSocket-Protocol: ");
  if (ppos != std::string::npos) {
    ppos += 24;
    auto pend = req.find("\r\n", ppos);
    std::string proto = req.substr(ppos, pend - ppos);
    // Take the first offered subprotocol (the auth token).
    auto comma = proto.find(',');
    if (comma != std::string::npos) proto = proto.substr(0, comma);
    // Trim surrounding whitespace.
    auto s = proto.find_first_not_of(" \t");
    auto e = proto.find_last_not_of(" \t");
    if (s != std::string::npos) proto = proto.substr(s, e - s + 1);
    if (!proto.empty()) resp += "\r\nSec-WebSocket-Protocol: " + proto;
  }
  resp += "\r\n\r\n";
  return resp;
}

// ========== HTTP helpers ==========

// Allowed CORS origins. Default: localhost for dev. Set via POKER_ALLOWED_ORIGINS env var
// (comma-separated). In production, set to your frontend domain.
static std::unordered_set<std::string> g_allowed_origins;

static void InitAllowedOrigins() {
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
  // Always allow localhost in dev.
  g_allowed_origins.insert("http://localhost:5173");
  g_allowed_origins.insert("http://localhost:3000");
  g_allowed_origins.insert("http://127.0.0.1:5173");
}

static bool IsOriginAllowed(const std::string& origin) {
  if (g_allowed_origins.empty()) return false;
  return g_allowed_origins.count(origin) > 0;
}

static std::string AllowedOriginHeader(const std::string& request_origin) {
  if (request_origin.empty() || !IsOriginAllowed(request_origin)) return "";
  return request_origin;
}

static std::string httpResponse(int code, const std::string& body,
                                 const std::string& request_origin = "") {
  std::string status = code == 200 ? "OK" : "Error";
  std::ostringstream r;
  r << "HTTP/1.1 " << code << " " << status << "\r\n";
  r << "Content-Type: application/json\r\n";
  r << "Content-Length: " << body.size() << "\r\n";
  if (!request_origin.empty()) {
    r << "Access-Control-Allow-Origin: " << request_origin << "\r\n";
  } else {
    r << "Access-Control-Allow-Origin: http://localhost:5173\r\n";
  }
  r << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
  r << "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n";
  // Security headers
  r << "X-Content-Type-Options: nosniff\r\n";
  r << "X-Frame-Options: DENY\r\n";
  r << "Content-Security-Policy: default-src 'self'; connect-src 'self' ws: wss:\r\n";
  r << "Strict-Transport-Security: max-age=31536000; includeSubDomains\r\n";
  r << "Connection: close\r\n\r\n";
  r << body;
  return r.str();
}

static std::string parseHttpBody(const std::string& req) {
  auto pos = req.find("\r\n\r\n");
  if (pos == std::string::npos) return "";
  return req.substr(pos + 4);
}

// Extract the Origin header value from an HTTP request string.
static std::string ExtractOriginHeader(const std::string& req) {
  auto header_end = req.find("\r\n\r\n");
  if (header_end == std::string::npos) return "";
  std::string headers = req.substr(0, header_end);
  // Case-insensitive search for "Origin:"
  auto pos = headers.find("Origin:");
  if (pos == std::string::npos) pos = headers.find("origin:");
  if (pos == std::string::npos) return "";
  pos += 7;  // skip "Origin:"
  while (pos < headers.size() && headers[pos] == ' ') ++pos;
  auto end = headers.find("\r\n", pos);
  if (end == std::string::npos) end = headers.size();
  return headers.substr(pos, end - pos);
}

static size_t parseContentLength(const std::string& req) {
  auto header_end = req.find("\r\n\r\n");
  if (header_end == std::string::npos) return 0;

  std::string headers = req.substr(0, header_end);
  std::transform(headers.begin(), headers.end(), headers.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  auto pos = headers.find("content-length:");
  if (pos == std::string::npos) return 0;
  pos += 15;
  while (pos < headers.size() && std::isspace(static_cast<unsigned char>(headers[pos]))) ++pos;

  size_t len = 0;
  while (pos < headers.size() && std::isdigit(static_cast<unsigned char>(headers[pos]))) {
    len = len * 10 + static_cast<size_t>(headers[pos] - '0');
    ++pos;
  }
  return len;
}

static bool ensureHttpRequestComplete(int fd, std::string& req) {
  constexpr int kTimeoutMs = 1000;
  constexpr size_t kMaxRequestBody = 1 * 1024 * 1024;  // 1 MB limit
  char extra[8192];

  auto readMore = [&]() -> bool {
    pollfd pfd{fd, POLLIN, 0};
    int ready = poll(&pfd, 1, kTimeoutMs);
    if (ready <= 0 || !(pfd.revents & POLLIN)) return false;

    int n = recv(fd, extra, sizeof(extra), 0);
    if (n <= 0) return false;
    req.append(extra, n);
    return true;
  };

  while (req.find("\r\n\r\n") == std::string::npos) {
    if (req.size() > 65536) return false;  // headers too large
    if (!readMore()) return false;
  }

  const size_t body_start = req.find("\r\n\r\n") + 4;
  const size_t content_length = parseContentLength(req);
  if (content_length > kMaxRequestBody) return false;  // reject oversized bodies
  while (req.size() < body_start + content_length) {
    if (!readMore()) return false;
  }
  return true;
}

// ========== JSON helpers ==========

static std::string jsonStr(const std::string& key, const std::string& val) {
  return "\"" + key + "\":\"" + val + "\"";
}
static std::string jsonNum(const std::string& key, double val) {
  std::ostringstream oss;
  oss << "\"" << key << "\":" << val;
  return oss.str();
}
static std::string jsonBool(const std::string& key, bool val) {
  return "\"" + key + "\":" + (val ? "true" : "false");
}
static std::string jsonGetStr(const std::string& json, const std::string& key) {
  auto p = json.find("\"" + key + "\"");
  if (p == std::string::npos) return "";
  p = json.find("\"", p + key.size() + 3);
  if (p == std::string::npos) return "";
  auto e = json.find("\"", p + 1);
  return json.substr(p + 1, e - p - 1);
}
static double jsonGetNum(const std::string& json, const std::string& key) {
  auto p = json.find("\"" + key + "\"");
  if (p == std::string::npos) return 0;
  p = json.find(':', p + key.size() + 1);
  if (p == std::string::npos) return 0;
  ++p;
  while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
  size_t e = p;
  while (e < json.size()) {
    char c = json[e];
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.') {
      ++e;
      continue;
    }
    break;
  }
  if (e == p) return 0;
  try {
    return std::stod(json.substr(p, e - p));
  } catch (...) {
    return 0;
  }
}



// Admin auth: validates Authorization: Bearer token against g_admin_token.
static bool IsAdminRequest(const std::string& req) {
  if (g_admin_token.empty()) return false;
  std::string token = poker_engine::cli::ExtractBearerToken(req);
  if (token.empty()) return false;
  // Constant-time comparison to avoid token timing side-channels.
  if (token.size() != g_admin_token.size()) return false;
  return CRYPTO_memcmp(token.data(), g_admin_token.data(), token.size()) == 0;
}

// Check if a player is banned (anti-cheat enforcement).
static bool IsPlayerBanned(int64_t player_id) {
  std::lock_guard<std::mutex> lock(g_ban_mutex);
  return g_banned_players.count(player_id) > 0;
}
static std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[7];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}


static std::string peerIp(int fd) {
  sockaddr_storage addr{};
  socklen_t len = sizeof(addr);
  if (getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return "unknown";
  char buf[INET6_ADDRSTRLEN] = {};
  if (addr.ss_family == AF_INET) {
    inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(&addr)->sin_addr, buf, sizeof(buf));
  } else if (addr.ss_family == AF_INET6) {
    inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(&addr)->sin6_addr, buf, sizeof(buf));
  } else {
    return "unknown";
  }
  return buf;
}


static void syncAuthChips(int64_t player_id, int64_t balance) {
  if (g_auth) g_auth->SetPlayerChips(player_id, balance);
}


static std::optional<std::string> validateTableBuyIn(const std::string& table_id, double buy_in) {
  double min_buy = 0;
  double max_buy = 0;
  if (!g_game || !g_game->GetTableBuyInLimits(table_id, min_buy, max_buy)) {
    return std::string("table_not_found");
  }
  poker_engine::cli::BuyInRange range{min_buy, max_buy};
  return poker_engine::cli::ValidateBuyInAmount(buy_in, range);
}

static bool debitBuyIn(int64_t player_id, int64_t amount, const std::string& table_id,
                       std::string& error) {
  if (player_id <= 0) return true;
  if (!g_db_initialized) return true;
  auto result =
      DatabaseManager::Instance().GetChipLedger().DebitBuyIn(player_id, amount, table_id);
  if (!result.success) {
    error = result.error;
    poker_engine::network::GlobalMetrics().buy_in_failures.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  syncAuthChips(player_id, result.balance_after);
  poker_engine::network::GlobalMetrics().buy_in_debits.fetch_add(1, std::memory_order_relaxed);

  // Record pending buy-in for crash recovery.
  {
    std::lock_guard<std::mutex> lock(g_pending_mutex);
    g_pending_buyins.emplace_back(player_id, amount, table_id);
  }
  return true;
}

static bool creditCashOut(int64_t player_id, int64_t amount, const std::string& table_id,
                          std::string& error) {
  if (player_id <= 0 || amount <= 0) return true;
  if (!g_db_initialized) return true;
  auto result =
      DatabaseManager::Instance().GetChipLedger().CreditCashOut(player_id, amount, table_id);
  if (!result.success) {
    error = result.error;
    return false;
  }
  syncAuthChips(player_id, result.balance_after);
  return true;
}

static int64_t getAccountChips(int64_t player_id) {
  if (player_id <= 0) return 0;
  if (g_db_initialized) {
    auto acct = DatabaseManager::Instance().GetAccountRepo().FindById(player_id);
    if (acct.has_value()) return acct->chips;
  }
  if (g_auth) {
    auto acct = g_auth->GetPlayer(player_id);
    if (acct.has_value()) return acct->chips;
  }
  return 0;
}

static bool leaveTableWithCashOut(int32_t player_id, const std::string& table_id, int64_t& cashed_out,
                                  int64_t& balance_after, std::string& error) {
  cashed_out = 0;
  balance_after = getAccountChips(player_id);
  error.clear();
  if (player_id <= 0) return false;
  if (table_id.empty()) return false;
  double stack = g_game->GetPlayerStack(table_id, player_id);
  if (!g_game->LeaveTable(player_id, table_id)) return false;
  UntrackPlayerTable(player_id, table_id);
  cashed_out = static_cast<int64_t>(stack);
  if (cashed_out > 0 && !creditCashOut(player_id, cashed_out, table_id, error)) {
    std::cout << "[WARN] Cash-out failed pid=" << player_id << " stack=" << cashed_out
              << " err=" << error << "\n";
  }
  balance_after = getAccountChips(player_id);
  return true;
}

static bool refundBuyIn(int64_t player_id, int64_t amount, const std::string& table_id,
                        std::string& error) {
  if (player_id <= 0 || amount <= 0) return true;
  if (!g_db_initialized) return true;
  auto result =
      DatabaseManager::Instance().GetChipLedger().CreditRefund(player_id, amount, table_id + ":rollback");
  if (!result.success) {
    error = result.error;
    return false;
  }
  syncAuthChips(player_id, result.balance_after);
  return true;
}

static bool seatPlayerWithBuyIn(int32_t player_id, const std::string& name, const std::string& table_id,
                                int seat_index, int64_t buy_in, const std::string& token) {
  if (auto buy_in_err = validateTableBuyIn(table_id, static_cast<double>(buy_in))) return false;
  std::string err;
  if (!debitBuyIn(player_id, buy_in, table_id, err)) return false;
  if (!g_game->JoinTable(player_id, table_id, name, seat_index, buy_in, token)) {
    refundBuyIn(player_id, buy_in, table_id, err);
    // Clean up pending entry on join failure.
    {
      std::lock_guard<std::mutex> lock(g_pending_mutex);
      g_pending_buyins.erase(
          std::remove_if(g_pending_buyins.begin(), g_pending_buyins.end(),
                         [&](const auto& p) { return p.player_id == player_id && p.table_id == table_id; }),
          g_pending_buyins.end());
    }
    return false;
  }
  // Join succeeded — clear pending entry and track for anti-cheat.
  {
    std::lock_guard<std::mutex> lock(g_pending_mutex);
    g_pending_buyins.erase(
        std::remove_if(g_pending_buyins.begin(), g_pending_buyins.end(),
                       [&](const auto& p) { return p.player_id == player_id && p.table_id == table_id; }),
        g_pending_buyins.end());
  }
  TrackPlayerTable(player_id, table_id);
  return true;
}

static bool authorizeHttpPlayer(const std::string& req, const std::string& body, int64_t claimed_id,
                                int64_t& verified_id) {
  std::string token = poker_engine::cli::ExtractBearerToken(req);
  if (token.empty()) token = jsonGetStr(body, "token");
  if (!poker_engine::cli::VerifyAuthToken(g_auth, token, verified_id)) return false;
  if (claimed_id > 0 && verified_id != claimed_id) return false;
  return true;
}

// Execute a parameterized query and return the result set.
// Replaces the dangerous sqlEscape + string concatenation pattern.
static poker_engine::phase14::ResultSet ParamQuery(const std::string& sql,
                                                    const std::vector<std::pair<int, int64_t>>& int_params = {}) {
  auto& db = DatabaseManager::Instance().GetDatabase();
  auto rs = db.Prepare(sql);
  poker_engine::phase14::StatementBinder binder(rs);
  for (const auto& [index, value] : int_params) {
    binder.Bind(index, value);
  }
  return rs;
}

static const char* streetName(int street) {
  switch (street) {
    case 0: return "preflop";
    case 1: return "flop";
    case 2: return "turn";
    case 3: return "river";
    default: return "unknown";
  }
}


using poker_engine::game::GameEvent;
using poker_engine::game::SeatState;
using poker_engine::game::Table;

static std::unordered_map<std::string, std::unordered_map<int32_t, double>> g_hand_start_chips;

struct PendingHandData {
  std::vector<ActionRecord> actions;
  std::unordered_map<int32_t, std::string> action_summaries;
};
static std::unordered_map<std::string, PendingHandData> g_pending_hands;

static const char* actionTypeDbName(poker_engine::game::ActionType type) {
  using poker_engine::game::ActionType;
  switch (type) {
    case ActionType::FOLD: return "FOLD";
    case ActionType::CHECK: return "CHECK";
    case ActionType::CALL: return "CALL";
    case ActionType::BET: return "BET";
    case ActionType::RAISE: return "RAISE";
    case ActionType::ALL_IN: return "ALL_IN";
    case ActionType::POST_SB: return "POST_SB";
    case ActionType::POST_BB: return "POST_BB";
    case ActionType::POST_ANTE: return "POST_ANTE";
    default: return "UNKNOWN";
  }
}

static const char* actionSummaryToken(poker_engine::game::ActionType type) {
  using poker_engine::game::ActionType;
  switch (type) {
    case ActionType::FOLD: return "fold";
    case ActionType::CHECK: return "check";
    case ActionType::CALL: return "call";
    case ActionType::BET: return "bet";
    case ActionType::RAISE: return "raise";
    case ActionType::ALL_IN: return "all-in";
    case ActionType::POST_SB: return "sb";
    case ActionType::POST_BB: return "bb";
    case ActionType::POST_ANTE: return "ante";
    default: return "?";
  }
}

static void recordAction(const std::string& table_id, const GameEvent& event) {
  if (!event.has_action) return;
  auto& pending = g_pending_hands[table_id];
  ActionRecord ar;
  ar.player_id = event.player_id;
  ar.street = event.street;
  ar.action_type = actionTypeDbName(event.action);
  ar.amount = event.amount;
  ar.pot_after = event.pot_after;
  ar.action_number = static_cast<int>(pending.actions.size()) + 1;
  pending.actions.push_back(ar);

  auto& summary = pending.action_summaries[event.player_id];
  if (!summary.empty()) summary += " / ";
  summary += actionSummaryToken(event.action);
  if (event.amount > 0.001) {
    summary += " $";
    summary += std::to_string(static_cast<int>(event.amount));
  }
}

static void fillBestHand(PlayerHandResult& r, const poker_engine::game::PlayerState& p,
                         const poker_engine::game::CommunityCards& comm) {
  if (!p.HasCards() || comm.count == 0) return;
  using poker_engine::game::ShowdownEvaluator;
  std::vector<uint8_t> hole = p.hole_cards.ToVector();
  std::vector<uint8_t> board;
  for (uint8_t i = 0; i < comm.count; ++i) board.push_back(comm.cards[i]);
  auto eval = ShowdownEvaluator::Evaluate(hole, board);
  r.best_hand = ShowdownEvaluator::HandRankName(eval.best_rank);
  r.hand_rank = eval.best_rank;
}



static std::string normalizeHoleCards(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c != '[' && c != ']' && c != ' ') out += c;
  }
  return out;
}

static void appendCommunityCards(const poker_engine::game::CommunityCards& comm,
                                 HandRecord& hand) {
  std::string board = comm.CardsStr();
  if (board.size() >= 2 && board.front() == '[' && board.back() == ']') {
    board = board.substr(1, board.size() - 2);
  }
  std::istringstream ss(board);
  std::string token;
  while (ss >> token) hand.community_cards.push_back(token);
}

// ---- Anti-cheat: live hand -> analytics snapshot ----

static void OnAnticheatAlert(const poker_engine::anticheat::CheatAlert& alert) {
  using namespace poker_engine;
  base::AuditLogEntry e;
  e.event_type = base::AuditEvent::SuspiciousActivity;
  e.player_id = alert.player_id;
  e.risk_score = static_cast<int>(alert.score);
  e.status = "reviewed";
  e.details["reason"] = alert.reason;
  e.details["evidence"] = alert.evidence;
  e.details["suspicion_level"] = static_cast<int>(alert.level);
  e.details["source"] = "anticheat_manager";
  base::StructuredAuditLogger::Instance().LogAsync(e);

  std::cout << "[ANTICHEAT] player=" << alert.player_id << " (" << alert.player_name
            << ") level=" << static_cast<int>(alert.level) << " score=" << alert.score
            << " reason=" << alert.reason << "\n";

  // ---- Graduated automated response ----
  using SuspicionLevel = poker_engine::anticheat::SuspicionLevel;
  auto pid = static_cast<int32_t>(alert.player_id);

  if (alert.level >= SuspicionLevel::High) {
    // Auto-kick: find all tables this player is on from global tracking.
    std::vector<std::string> to_leave;
    {
      std::lock_guard<std::mutex> lock(g_player_tables_mutex);
      for (const auto& e : g_player_tables) {
        if (e.player_id == pid) to_leave.push_back(e.table_id);
      }
    }
    for (const auto& tid : to_leave) {
      if (g_game) g_game->LeaveTable(pid, tid);
      UntrackPlayerTable(pid, tid);
      std::cout << "[ANTICHEAT] Kicked player " << alert.player_id << " from " << tid << "\n";
    }
  }

  if (alert.level >= SuspicionLevel::Confirmed) {
    // Permanent ban.
    {
      std::lock_guard<std::mutex> lock(g_ban_mutex);
      g_banned_players.insert(alert.player_id);
      SaveBans();  // persist ban immediately
    }
    std::cout << "[ANTICHEAT] BANNED player " << alert.player_id << " (" << alert.player_name << ")\n";
    // Revoke all sessions.
    if (g_auth) g_auth->GetTokenService().RevokeAll(alert.player_id);
    // Force-close any still-open WebSocket connections for this player so the
    // ban takes effect immediately (no waiting for reconnect).
    KickPlayerConnections(static_cast<int32_t>(alert.player_id));
  }
}

// Build a replay::HandSnapshot from a completed hand so the anti-cheat
// manager can update per-player statistics and (after RunAnalysis) emit alerts.
static poker_engine::replay::HandSnapshot BuildHandSnapshot(
    const std::string& table_id, Table& table,
    const std::vector<poker_engine::phase14::PlayerHandResult>& results,
    const std::vector<poker_engine::phase14::ActionRecord>& actions) {
  using namespace poker_engine;
  const auto& gs = table.GetGameState();
  replay::HandSnapshot snap;

  const auto& start_chips = g_hand_start_chips[table_id];
  for (const auto& r : results) {
    replay::HandSnapshot::PlayerSnap ps;
    ps.player_id = r.player_id;
    ps.display_name = r.player_name;
    ps.chips_at_start = start_chips.count(r.player_id) ? start_chips.at(r.player_id) : 0;
    ps.chips_at_end = ps.chips_at_start + static_cast<int64_t>(r.net_profit);
    ps.net_profit = static_cast<int64_t>(r.net_profit);
    ps.is_winner = r.won;
    // Per-action strings the collusion/bot detectors expect ("call N", "raise N").
    for (const auto& a : actions) {
      if (a.player_id != r.player_id) continue;
      ps.actions.push_back(a.action_type + " " + std::to_string(static_cast<int>(a.amount)));
    }
    snap.players.push_back(ps);
  }
  const auto& comm = gs.GetCommunity();
  for (uint8_t i = 0; i < comm.count; ++i) snap.community_cards.push_back(comm.cards[i]);
  snap.total_pot = static_cast<int64_t>(gs.GetPot());
  return snap;
}

static void persistCompletedHand(const std::string& table_id, Table& table) {
  if (!g_db_initialized) return;

  const auto& gs = table.GetGameState();
  const auto& settings = table.GetSettings();
  HandRecord hand;
  hand.table_name = table_id;
  hand.num_players = table.PlayerCount();
  hand.small_blind = settings.small_blind;
  hand.big_blind = settings.big_blind;
  hand.ante = settings.ante;
  hand.rng_proof = gs.GetLastRngProof();  // persist commit-reveal fairness proof
  appendCommunityCards(gs.GetCommunity(), hand);

  std::vector<PlayerHandResult> results;
  for (const auto& p : gs.AllPlayers()) {
    if (p.seat_state == SeatState::EMPTY) continue;
    PlayerHandResult r;
    r.player_id = p.id;
    r.player_name = p.name;
    if (p.HasCards()) r.hole_cards = normalizeHoleCards(p.hole_cards.ToString());
    r.amount_wagered = p.bet_info.total_invested;
    double start_chips = r.amount_wagered > 0 ? p.chips + r.amount_wagered : p.chips;
    auto table_it = g_hand_start_chips.find(table_id);
    if (table_it != g_hand_start_chips.end()) {
      auto pit = table_it->second.find(p.id);
      if (pit != table_it->second.end()) start_chips = pit->second;
    }
    r.net_profit = p.chips - start_chips;
    r.amount_won = r.net_profit > 0 ? r.net_profit : 0;
    r.won = r.net_profit > 0.001;
    results.push_back(r);
  }

  auto pending_it = g_pending_hands.find(table_id);
  if (pending_it != g_pending_hands.end()) {
    for (auto& r : results) {
      auto sum_it = pending_it->second.action_summaries.find(r.player_id);
      if (sum_it != pending_it->second.action_summaries.end()) {
        r.action_summary = sum_it->second;
      }
    }
  }

  const auto& comm = gs.GetCommunity();
  for (auto& r : results) {
    for (const auto& p : gs.AllPlayers()) {
      if (p.id == r.player_id) {
        fillBestHand(r, p, comm);
        break;
      }
    }
  }

  std::vector<ActionRecord> actions;
  if (pending_it != g_pending_hands.end()) actions = pending_it->second.actions;

  auto hand_id = DatabaseManager::Instance().GetHandRepo().SaveHand(hand, results, actions);
  if (hand_id > 0) {
    std::cout << "[DB] Saved hand #" << hand_id << " from table " << table_id
              << " (" << actions.size() << " actions)\n";

    // Anti-cheat: feed the completed hand to the live detector.
    auto snap = BuildHandSnapshot(table_id, table, results, actions);
    snap.hand_id = hand_id;
    g_anticheat.SubmitHandData(snap);
    ++g_anticheat_hand_counter;
    g_anticheat.RunAnalysis();  // Real-time: analyze every hand.

    // 成就：按每玩家当前手数/胜负/连胜检查并解锁
    for (const auto& r : results) {
      int64_t streak = 0;
      if (r.won) {
        streak = ++g_win_streak[r.player_id];
      } else {
        g_win_streak[r.player_id] = 0;
      }
      auto acct = DatabaseManager::Instance().GetAccountRepo().FindById(r.player_id);
      int64_t hp = acct.has_value() ? acct->hands_played : 0;
      auto newly =
          DatabaseManager::Instance().GetAccountRepo().CheckAchievements(
              r.player_id, hp, r.won, streak);
      for (const auto& a : newly) {
        std::cout << "[ACHV] player " << r.player_id << " unlocked " << a << "\n";
      }
    }
  }
}

static void onTableGameEvent(const std::string& table_id, Table& table, const GameEvent& event) {
  if (event.type == GameEvent::HAND_STARTED) {
    g_hand_start_chips[table_id].clear();
    g_pending_hands[table_id] = PendingHandData{};
    for (const auto& p : table.GetGameState().AllPlayers()) {
      if (p.seat_state != SeatState::EMPTY) g_hand_start_chips[table_id][p.id] = p.chips;
    }
    return;
  }
  if (event.type == GameEvent::ACTION_TAKEN) {
    recordAction(table_id, event);
    return;
  }
  if (event.type == GameEvent::HAND_COMPLETE) {
    persistCompletedHand(table_id, table);
    g_hand_start_chips.erase(table_id);
    g_pending_hands.erase(table_id);
    if (table.PlayerCount() >= 2) table.StartHand();
  }
}

static void setupHandPersistence() {
  if (!g_db_initialized) return;
  g_game->SetTableGameEventCallback(onTableGameEvent);
}

// ========== Main ==========

int main(int argc, char* argv[]) {
  int port = 9001;
  if (argc > 1) port = std::atoi(argv[1]);

  auto runtime_cfg = poker_engine::cli::LoadServerRuntimeConfig();
  g_production_mode = runtime_cfg.production_mode;
  g_admin_token = runtime_cfg.admin_token;
  InitAllowedOrigins();

  // Load backpressure limits from environment.
  if (const char* v = std::getenv("POKER_MAX_CLIENTS")) g_max_clients = std::atoi(v);
  if (g_max_clients <= 0) g_max_clients = 500;
  if (const char* v = std::getenv("POKER_MAX_TABLES")) g_max_tables = std::atoi(v);
  if (g_max_tables <= 0) g_max_tables = 100;

  std::cout << "=== Poker Engine WebSocket Server v2.0 ===\nPort: " << port
            << " mode=" << (g_production_mode ? "production" : "development")
            << " max_clients=" << g_max_clients << " max_tables=" << g_max_tables << "\n";

  g_auth = new AuthService(runtime_cfg.jwt_secret);
  g_instance_id = runtime_cfg.instance_id;
  g_session_store = std::make_shared<poker_engine::network::DistributedSessionStore>(g_instance_id);
  if (!runtime_cfg.redis_url.empty()) {
    if (g_session_store->ConnectRedis(runtime_cfg.redis_url)) {
      g_redis_connected = true;
      g_auth->GetTokenService().SetRevocationBackend(g_session_store);
      g_redis_heartbeat_thread = std::thread([]() {
        while (g_running) {
          if (g_session_store) g_session_store->Heartbeat(30);
          std::this_thread::sleep_for(std::chrono::seconds(10));
        }
      });
      std::cout << "[Server] Redis session store connected (" << runtime_cfg.redis_url << ")\n";
    } else {
      std::cout << "[WARN] Redis connection failed: " << runtime_cfg.redis_url << "\n";
    }
  }
  g_game = new GameServer();
  g_db_initialized = DatabaseManager::Instance().Initialize(runtime_cfg.db_path);
  if (g_db_initialized) {
    std::cout << "[Server] Database initialized (" << runtime_cfg.db_path << ")\n";
    // Wire AuthService to persistent account storage
    g_auth->SetAccountRepository(&DatabaseManager::Instance().GetAccountRepo());
    std::cout << "[Server] Account persistence enabled\n";

    // Recover any orphaned buy-ins from a previous crash.
    RecoverOrphanedBuyIns();
    LoadBans();

    // Anti-cheat: route generated alerts into the structured audit log.
    // Place the audit log alongside the database for easy correlation.
    std::string audit_path = runtime_cfg.db_path;
    auto slash = audit_path.find_last_of("/\\");
    std::string audit_dir = (slash != std::string::npos) ? audit_path.substr(0, slash + 1) : "./";
    if (poker_engine::base::StructuredAuditLogger::Instance().Initialize(audit_dir)) {
      g_anticheat.SetAlertCallback(OnAnticheatAlert);
      std::cout << "[Server] Anti-cheat audit logging enabled\n";
    }
    if (!runtime_cfg.postgres_url.empty()) {
      if (DatabaseManager::Instance().ConnectPostgresMirror(runtime_cfg.postgres_url)) {
        std::cout << "[Server] PostgreSQL wallet mirror active\n";
      } else {
        std::cout << "[WARN] PostgreSQL mirror unavailable\n";
      }
    }
  } else {
    std::cout << "[WARN] Database init failed — hand history API unavailable\n";
  }
  g_game->CreateTable("main", 6, 1, 2);
  g_game->CreateTable("table_1", 6, 1, 2);
  // 多盲注级别牌桌，供大厅筛选演示
  g_game->CreateTable("micro", 6, 0.5, 1);
  g_game->CreateTable("small", 6, 2, 4);
  g_game->CreateTable("mid", 6, 5, 10);
  g_game->CreateTable("high", 6, 10, 20);
  if (const char* cfr_model = std::getenv("POKER_CFR_MODEL_PATH")) {
    if (cfr_model[0] != '\0') {
      if (poker_engine::network::CfrPolicyStore::Instance().LoadFromFile(cfr_model)) {
        std::cout << "[Server] CFR bot policy loaded: " << cfr_model
                  << " nodes=" << poker_engine::network::CfrPolicyStore::Instance().NodeCount() << "\n";
      } else {
        std::cout << "[WARN] Failed to load CFR model: " << cfr_model << " (bots use rule-based AI)\n";
      }
    }
  }

  g_tourn = new TournamentServer();
  // Create a default freezeout tournament
  TournamentConfig default_tourn;
  default_tourn.name = "每日锦标赛";
  default_tourn.type = TournamentType::Freezeout;
  default_tourn.starting_stack = 1500;
  default_tourn.max_players = 100;
  default_tourn.players_per_table = 6;
  default_tourn.buy_in = 10.0;
  default_tourn.blind_schedule = TournamentConfig::GenerateTurboBlinds();
  g_tourn->CreateTournament(default_tourn);
  std::cout << "[Server] Default tournament created\n";
  if (!g_production_mode) {
    g_game->AddBots("main", 3, 200);
    g_game->AddBots("table_1", 3, 200);
    std::cout << "[Server] Default tables created, main/table_1 each have 3 bots waiting\n";
    setupHandPersistence();
    g_game->StartGame("main");
    g_game->StartGame("table_1");
    std::cout << "[Server] Auto-started bot tables\n";
  } else {
    setupHandPersistence();
    std::cout << "[Server] Production mode: no default bots/auto-start\n";
  }

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "[FATAL] socket failed: errno=" << errno << " " << strerror(errno) << "\n";
    delete g_game;
    delete g_auth;
    return 1;
  }

  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    std::cerr << "[FATAL] setsockopt(SO_REUSEADDR) failed: errno=" << errno << " "
              << strerror(errno) << "\n";
    close(server_fd);
    delete g_game;
    delete g_auth;
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
    std::cerr << "[FATAL] bind failed on port " << port << ": errno=" << errno << " "
              << strerror(errno) << "\n";
    close(server_fd);
    delete g_game;
    delete g_auth;
    return 1;
  }
  if (listen(server_fd, 10) < 0) {
    std::cerr << "[FATAL] listen failed on port " << port << ": errno=" << errno << " "
              << strerror(errno) << "\n";
    close(server_fd);
    delete g_game;
    delete g_auth;
    return 1;
  }

  std::cout << "[Server] Listening on port " << port << "\n";

  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  // Alias the file-scope WS client registry so the existing loop code keeps
  // working unchanged.
  auto& clients = g_ws_clients;
  auto& clients_mutex = g_ws_clients_mutex;

  // Spectator delay: hole cards hidden, state updates delayed.
  static int g_spectator_delay_secs = 120;
  if (const char* v = std::getenv("POKER_SPECTATOR_DELAY_SECS")) {
    g_spectator_delay_secs = std::atoi(v);
    if (g_spectator_delay_secs < 0) g_spectator_delay_secs = 0;
  }

  g_game->SetBroadcastCallback([&](const std::string& tableId, const std::string& /*msg*/) {
    std::lock_guard<std::recursive_mutex> lock(clients_mutex);
    for (auto& [fd, cl] : clients) {
      if (cl.table_id != tableId) continue;
      std::string stateJson = g_game->GetTableStateJSON(tableId, cl.player_id);
      std::string wrapped =
          "{\"type\":\"table_state\",\"seq\":0,\"timestamp\":\"\",\"payload\":" + stateJson + "}";
      auto encoded = wsEncode(wrapped);
      ssize_t sent = ::send(fd, encoded.c_str(), encoded.size(), MSG_NOSIGNAL);
      if (sent < (ssize_t)encoded.size()) {
        std::cout << "[WARN] Broadcast short write fd=" << fd << " " << sent << "/"
                  << encoded.size() << "\n";
      }
    }
  });

  auto urlDecode = [](const std::string& s) -> std::string {
    std::string r;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '%' && i + 2 < s.size()) {
        int hi = s[i + 1], lo = s[i + 2];
        auto hex = [](int c) -> int {
          if (c >= '0' && c <= '9') return c - '0';
          if (c >= 'a' && c <= 'f') return c - 'a' + 10;
          if (c >= 'A' && c <= 'F') return c - 'A' + 10;
          return 0;
        };
        r += (char)(hex(hi) * 16 + hex(lo));
        i += 2;
      } else {
        r += s[i];
      }
    }
    return r;
  };

  auto sendTo = [](int fd, const std::string& msg) {
    auto encoded = wsEncode(msg);
    ssize_t sent = send(fd, encoded.c_str(), encoded.size(), MSG_NOSIGNAL);
    if (sent < (ssize_t)encoded.size()) {
      std::cout << "[WARN] sendTo short write: fd=" << fd << " sent=" << sent << "/"
                << encoded.size() << " errno=" << errno << " " << strerror(errno) << "\n";
    }
  };

  auto sendClose = [](int fd) {
    std::string frame;
    frame += (char)0x88;  // FIN + close opcode
    frame += (char)0x00;  // zero length (no body)
    send(fd, frame.c_str(), frame.size(), MSG_NOSIGNAL);
  };

  auto makeMsg = [](const std::string& type, const std::string& payloadJson) -> std::string {
    return "{\"type\":\"" + type + "\",\"seq\":0,\"timestamp\":\"\",\"payload\":" + payloadJson +
           "}";
  };

  // ---- In-table chat (commercial social feature) ----
  // Per-table rolling history so a late joiner sees recent messages.
  static const size_t kChatHistoryMax = 50;
  static std::unordered_map<std::string, std::deque<std::string>> g_chat_history;
  static std::mutex g_chat_mutex;

  auto nowMs = []() -> int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  };

  // 每日奖励用：当前日期与昨日日期（YYYY-MM-DD）
  auto dateStr = [](std::chrono::system_clock::time_point tp) -> std::string {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    localtime_r(&tt, &tm_buf);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return std::string(buf);
  };
  auto todayStr = [&]() { return dateStr(std::chrono::system_clock::now()); };
  auto yesterdayStr = [&]() {
    return dateStr(std::chrono::system_clock::now() - std::chrono::hours(24));
  };
  // 连续领取天数 -> 奖励筹码（基础100，每天+50，封顶500）
  auto dailyReward = [](int64_t streak) -> int64_t {
    if (streak < 1) streak = 1;
    int64_t r = 100 + (streak - 1) * 50;
    return r > 500 ? 500 : r;
  };

  // Strip control chars and cap length to keep the WS channel safe.
  auto sanitizeChat = [](const std::string& raw) -> std::string {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
      unsigned char u = (unsigned char)c;
      if (u < 0x20 && c != '\n' && c != '\t') continue;  // drop control chars
      out += c;
    }
    if (out.size() > 400) out.resize(400);
    // collapse repeated whitespace, trim
    std::string trimmed;
    bool space = false;
    for (char c : out) {
      if (c == ' ' || c == '\n' || c == '\t') {
        if (!space && !trimmed.empty()) trimmed += ' ';
        space = true;
      } else {
        trimmed += c;
        space = false;
      }
    }
    return trimmed;
  };

  // Mask profanity / scam words and external links from a chat message.
  // Case-insensitive substring masking; matched byte ranges are replaced with '*'.
  auto filterProfanity = [](std::string s) -> std::string {
    static const std::vector<std::string> kBadWords = {
        // 中文辱骂/违规
        "傻逼", "煞笔", "妈的", "操你", "草你", "垃圾", "贱人", "滚蛋", "去死",
        "赌博", "博彩", "赌钱", "色情", "约炮", "代练", "外挂", "开挂", "作弊",
        // 诈骗/广告拉人（免费应用尤其要挡）
        "加微信", "加我微信", "私聊", "充值", "优惠", "免费送", "加群", "代理",
        // 英文
        "fuck", "shit", "bitch", "asshole", "bastard", "idiot", "scam",
        "cheat", "hack", "casino", "porn", "dick", "cunt", "whore",
    };
    std::string low;
    low.reserve(s.size());
    for (char c : s) low += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::vector<std::pair<size_t, size_t>> ranges;
    for (const auto& w : kBadWords) {
      if (w.empty()) continue;
      size_t pos = 0;
      while ((pos = low.find(w, pos)) != std::string::npos) {
        ranges.push_back({pos, pos + w.size()});
        pos += w.size();
      }
    }
    // 外部链接（引流/诈骗）整段屏蔽
    static const std::vector<std::string> kUrlPrefix = {"http://", "https://", "www."};
    for (const auto& p : kUrlPrefix) {
      size_t pos = 0;
      while ((pos = low.find(p, pos)) != std::string::npos) {
        size_t end = s.find_first_of(" \t\r\n", pos);
        if (end == std::string::npos) end = s.size();
        ranges.push_back({pos, end});
        if (end == s.size()) break;
        pos = end + 1;
      }
    }
    if (ranges.empty()) return s;

    std::sort(ranges.begin(), ranges.end());
    std::vector<std::pair<size_t, size_t>> merged;
    for (auto& r : ranges) {
      if (!merged.empty() && r.first <= merged.back().second) {
        merged.back().second = std::max(merged.back().second, r.second);
      } else {
        merged.push_back(r);
      }
    }
    std::string out;
    out.reserve(s.size());
    size_t cursor = 0;
    for (auto& r : merged) {
      if (r.first > cursor) out += s.substr(cursor, r.first - cursor);
      out += std::string(r.second - r.first, '*');
      cursor = r.second;
    }
    if (cursor < s.size()) out += s.substr(cursor);
    return out;
  };

  // Broadcast a chat message to every client sitting at the given table.
  auto broadcastChat = [&](const std::string& tableId, const std::string& chatJson) {
    std::lock_guard<std::recursive_mutex> lock(clients_mutex);
    auto encoded = wsEncode(chatJson);
    int sent_count = 0;
    for (auto& [fd, cl] : clients) {
      if (cl.table_id != tableId) continue;
      ++sent_count;
      ssize_t sent = ::send(fd, encoded.c_str(), encoded.size(), MSG_NOSIGNAL);
      if (sent < (ssize_t)encoded.size()) {
        std::cout << "[WARN] chat broadcast short write fd=" << fd << " " << sent << "/"
                  << encoded.size() << "\n";
      }
    }
    std::cerr << "[DBG] broadcastChat table=" << tableId << " recipients=" << sent_count << "\n";
  };

  auto leaveCurrentTable = [&](WsClient& cl) {
    std::string tableId = cl.table_id;
    cl.table_id.clear();
    if (cl.player_id > 0 && !tableId.empty()) {
      int64_t cashed_out = 0;
      int64_t balance_after = 0;
      std::string ledger_err;
      leaveTableWithCashOut(cl.player_id, tableId, cashed_out, balance_after, ledger_err);
      if (cashed_out > 0) {
        std::cout << "[Server] Auto cash-out on disconnect pid=" << cl.player_id << " amount="
                  << cashed_out << " balance=" << balance_after << "\n";
      }
    }
    if (cl.player_id > 0 && g_session_store && g_session_store->IsRedisConnected()) {
      g_session_store->ClearPlayerInstance(cl.player_id);
    }
  };

  auto runBotTurns = [&](const std::string& tableId, int maxIters = 30) {
    for (int bi = 0; bi < maxIters; ++bi) {
      std::string stateJson = g_game->GetTableStateJSON(tableId);
      if (stateJson.find("\"status\":\"playing\"") == std::string::npos) break;
      bool botTurn = false;
      auto cpPos = stateJson.find("\"current_player_id\":");
      if (cpPos != std::string::npos) {
        int cpId = 0;
        auto numStart = stateJson.find_first_of("-0123456789", cpPos + 20);
        if (numStart != std::string::npos) cpId = std::atoi(stateJson.c_str() + numStart);
        if (cpId < 0) botTurn = true;
      }
      if (!botTurn) break;
      g_game->ProcessBotActions(tableId);
    }
  };

  while (g_running) {
    std::vector<pollfd> pollfds;
    pollfds.push_back({server_fd, POLLIN, 0});
    {
      std::lock_guard<std::recursive_mutex> lock(clients_mutex);
      for (auto& [fd, _] : clients) {
        pollfds.push_back({fd, POLLIN, 0});
      }
    }

    int poll_result = poll(pollfds.data(), pollfds.size(), 500);

    if (poll_result > 0 && (pollfds[0].revents & POLLIN)) {
      int client_fd = accept(server_fd, nullptr, nullptr);
      if (client_fd >= 0) {
        std::lock_guard<std::recursive_mutex> lock(clients_mutex);
        if (static_cast<int>(clients.size()) >= g_max_clients) {
          close(client_fd);  // reject — at capacity
          continue;
        }
        WsClient client;
        client.fd = client_fd;
        clients[client_fd] = std::move(client);
      }
    }

    if (poll_result <= 0) {
      // No client I/O — still advance bot turns
      for (int i = 0; i < 10; ++i) {
        std::string stateMain = g_game->GetTableStateJSON("main");
        bool mainBotTurn = false;
        auto cpPos = stateMain.find("\"current_player_id\":");
        if (cpPos != std::string::npos) {
          int cpId = 0;
          auto numStart = stateMain.find_first_of("-0123456789", cpPos + 20);
          if (numStart != std::string::npos) cpId = std::atoi(stateMain.c_str() + numStart);
          if (cpId < 0) mainBotTurn = true;
        }
        if (!mainBotTurn) break;
        g_game->ProcessBotActions("main");
      }
      for (int i = 0; i < 10; ++i) {
        std::string stateT1 = g_game->GetTableStateJSON("table_1");
        bool t1BotTurn = false;
        auto cpPos = stateT1.find("\"current_player_id\":");
        if (cpPos != std::string::npos) {
          int cpId = 0;
          auto numStart = stateT1.find_first_of("-0123456789", cpPos + 20);
          if (numStart != std::string::npos) cpId = std::atoi(stateT1.c_str() + numStart);
          if (cpId < 0) t1BotTurn = true;
        }
        if (!t1BotTurn) break;
        g_game->ProcessBotActions("table_1");
      }
      continue;
    }

    std::unordered_set<int> ready_fds;
    for (size_t i = 1; i < pollfds.size(); ++i) {
      if (pollfds[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) {
        ready_fds.insert(pollfds[i].fd);
      }
    }

    std::lock_guard<std::recursive_mutex> lock(clients_mutex);
    for (auto it = clients.begin(); it != clients.end();) {
      int fd = it->first;
      if (ready_fds.count(fd) == 0) {
        ++it;
        continue;
      }

      char buf[8192];
      int n = recv(fd, buf, sizeof(buf) - 1, 0);
      if (n <= 0) {
        if (it->second.player_id > 0) {
          std::cout << "[Server] WS client disconnected (fd=" << fd
                    << ") player=" << it->second.name << " n=" << n << " errno=" << errno << "\n";
          leaveCurrentTable(it->second);
        } else {
          std::cout << "[Server] HTTP client disconnected (fd=" << fd << ") n=" << n
                    << " errno=" << errno << "\n";
        }
        close(fd);
        it = clients.erase(it);
        continue;
      }
      buf[n] = 0;
      std::string data(buf, n);
      if (data.find("GET ") == 0 || data.find("POST ") == 0 || data.find("OPTIONS ") == 0) {
        if (!ensureHttpRequestComplete(fd, data)) {
          std::cout << "[WARN] Incomplete HTTP request fd=" << fd << "\n";
          close(fd);
          it = clients.erase(it);
          continue;
        }
        poker_engine::network::GlobalMetrics().http_requests.fetch_add(1, std::memory_order_relaxed);
      }

      // HTTP GET /health — public liveness probe (minimal info).
      if (data.find("GET /health ") == 0) {
        std::string resp = httpResponse(200, "{\"status\":\"ok\"}");
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /health/detailed — authenticated detailed status (admin only).
      if (data.find("GET /health/detailed") == 0) {
        if (!IsAdminRequest(data)) {
          std::string resp = httpResponse(401, "{\"error\":\"unauthorized\"}");
          send(fd, resp.c_str(), resp.size(), 0); close(fd); it = clients.erase(it); continue;
        }
        poker_engine::network::HealthSnapshot health;
        health.production_mode = g_production_mode;
        health.db_healthy = g_db_initialized && DatabaseManager::Instance().IsHealthy();
        health.table_count = g_game ? g_game->TableCount() : 0;
        health.redis_healthy = g_redis_connected.load();
        health.instance_id = g_instance_id;
        health.postgres_healthy = g_db_initialized && DatabaseManager::Instance().IsPostgresMirrorConnected();
        std::string resp = httpResponse(200, poker_engine::network::BuildHealthJson(health));
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /metrics — Prometheus scrape endpoint
      if (data.find("GET /metrics") == 0) {
        poker_engine::network::HealthSnapshot health;
        health.production_mode = g_production_mode;
        health.db_healthy = g_db_initialized && DatabaseManager::Instance().IsHealthy();
        health.table_count = g_game ? g_game->TableCount() : 0;
        health.redis_healthy = g_redis_connected.load();
        health.instance_id = g_instance_id;
        health.postgres_healthy = g_db_initialized && DatabaseManager::Instance().IsPostgresMirrorConnected();
        const std::string body = poker_engine::network::RenderPrometheusMetrics(
            poker_engine::network::GlobalMetrics(), health);
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; version=0.0.4\r\nContent-Length: " +
                           std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /api/tables (must be before WebSocket GET / handler)
      // HTTP GET /leaderboard — standalone leaderboard website
      if (data.find("GET /leaderboard") == 0) {
        std::ifstream lb_file("frontend/dist/leaderboard.html");
        std::string html;
        if (lb_file.is_open()) {
          std::ostringstream ss;
          ss << lb_file.rdbuf();
          html = ss.str();
        } else {
          html = "<html><body><h1>Leaderboard unavailable</h1></body></html>";
        }
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                           std::to_string(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }
      if (data.find("GET /api/tables") == 0) {
        std::string respBody = g_game->GetTablesListJSON();
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /api/hands/<id> — hand detail with actions
      if (data.find("GET /api/hands/") == 0) {
        std::string respBody = "{}";
        if (g_db_initialized) {
          auto path_end = data.find(" ", 15);
          std::string id_str = data.substr(15, path_end - 15);
          int64_t hand_id = std::atoll(id_str.c_str());

          // Get hand record — parameterized query (defense-in-depth).
          auto rs = ParamQuery(
              "SELECT h.hand_id, h.table_name, h.community_cards, h.timestamp, "
              "COALESCE((SELECT MAX(a.pot_after) FROM actions a WHERE a.hand_id = h.hand_id), "
              "(SELECT SUM(pr.amount_won) FROM player_results pr WHERE pr.hand_id = h.hand_id), 0), "
              "h.rng_proof "
              "FROM hands h WHERE h.hand_id = ?",
              {{1, hand_id}});
          std::string hand_json = "{}";
          while (rs.Next()) {
            auto row = rs.GetRow();
            int64_t hid = row.GetInt64(0);
            std::ostringstream h;
            h << "{\"hand_id\":" << hid << ",\"table_id\":\"" << jsonEscape(row.GetString(1))
              << "\""
              << ",\"hand_number\":" << hid << ",\"community_cards\":\""
              << jsonEscape(row.GetString(2)) << "\""
              << ",\"pot_amount\":" << row.GetDouble(4) << ",\"timestamp\":\"" << jsonEscape(row.GetString(3))
              << "\",\"rng_proof\":\"" << jsonEscape(row.GetString(5)) << "\"}";
            hand_json = h.str();
            break;
          }

          // Get player results — parameterized
          auto pr_rs = ParamQuery(
              "SELECT player_id, player_name, hole_cards, action_summary, "
              "amount_won, net_profit, won, best_hand "
              "FROM player_results WHERE hand_id = ?",
              {{1, hand_id}});
          std::ostringstream pr_json;
          pr_json << "[";
          bool first = true;
          while (pr_rs.Next()) {
            auto row = pr_rs.GetRow();
            if (!first) pr_json << ",";
            first = false;
            pr_json << "{\"player_id\":" << row.GetInt64(0) << ",\"name\":\"" << jsonEscape(row.GetString(1))
                    << "\""
                    << ",\"hole_cards\":\"" << jsonEscape(row.GetString(2)) << "\""
                    << ",\"action_summary\":\"" << jsonEscape(row.GetString(3)) << "\""
                    << ",\"amount_won\":" << row.GetDouble(4)
                    << ",\"net_profit\":" << row.GetDouble(5) << ",\"won\":" << row.GetInt(6)
                    << ",\"best_hand\":\"" << jsonEscape(row.GetString(7)) << "\"}";
          }
          pr_json << "]";

          // Get actions — parameterized
          auto act_rs = ParamQuery(
              "SELECT player_id, street, action_type, amount, pot_after "
              "FROM actions WHERE hand_id = ? ORDER BY action_number",
              {{1, hand_id}});
          std::ostringstream act_json;
          act_json << "[";
          bool first_act = true;
          while (act_rs.Next()) {
            auto row = act_rs.GetRow();
            if (!first_act) act_json << ",";
            first_act = false;
            act_json << "{\"player_id\":" << row.GetInt64(0) << ",\"street\":\""
                     << streetName(row.GetInt(1)) << "\""
                     << ",\"action\":\"" << jsonEscape(row.GetString(2)) << "\""
                     << ",\"amount\":" << row.GetDouble(3) << ",\"pot_after\":" << row.GetDouble(4)
                     << "}";
          }
          act_json << "]";

          // Combine (only when hand exists)
          if (hand_json != "{}") {
            hand_json.pop_back();  // remove trailing }
            hand_json += ",\"players\":" + pr_json.str();
            hand_json += ",\"actions\":" + act_json.str();
            hand_json += "}";
            respBody = hand_json;
          }
        }
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /api/hands — list recent hands
      if (data.find("GET /api/hands") == 0) {
        std::string respBody = "[]";
        if (g_db_initialized) {
          int limit = 50;
          auto qpos = data.find("limit=");
          if (qpos != std::string::npos) {
            limit = std::atoi(data.c_str() + qpos + 6);
            if (limit <= 0 || limit > 200) limit = 50;
          }
          auto rs = ParamQuery(
              "SELECT h.hand_id, h.table_name, h.community_cards, h.timestamp, "
              "COALESCE((SELECT MAX(a.pot_after) FROM actions a WHERE a.hand_id = h.hand_id), "
              "(SELECT SUM(pr.amount_won) FROM player_results pr WHERE pr.hand_id = h.hand_id), 0) "
              "FROM hands h ORDER BY h.hand_id DESC LIMIT ?",
              {{1, static_cast<int64_t>(limit)}});
          std::ostringstream json;
          json << "[";
          bool first = true;
          while (rs.Next()) {
            auto row = rs.GetRow();
            if (!first) json << ",";
            first = false;
            int64_t hand_id = row.GetInt64(0);
            json << "{\"hand_id\":" << hand_id << ",\"table_id\":\"" << jsonEscape(row.GetString(1))
                 << "\""
                 << ",\"hand_number\":" << hand_id << ",\"community_cards\":\""
                 << jsonEscape(row.GetString(2)) << "\""
                 << ",\"pot_amount\":" << row.GetDouble(4) << ",\"timestamp\":\""
                 << jsonEscape(row.GetString(3)) << "\"}";
          }
          json << "]";
          respBody = json.str();
        }
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /api/players/<id>/stats — player statistics
      if (data.find("GET /api/players/") == 0 && data.find("/stats", 16) != std::string::npos) {
        std::string respBody = "{}";
        if (g_db_initialized) {
          auto path_end = data.find(" ", 16);
          std::string path = data.substr(16, path_end - 16);
          auto slash = path.find("/stats");
          int32_t pid = std::atoi(path.substr(0, slash).c_str());

          auto& dm = DatabaseManager::Instance();
          auto stats = dm.GetPlayerRepo().CalculateStats(pid);
          auto info = dm.GetPlayerRepo().GetPlayer(pid);

          std::ostringstream j;
          j << "{\"player_id\":" << stats.player_id << ",\"name\":\"" << info.name << "\""
            << ",\"display_name\":\"" << info.display_name << "\""
            << ",\"hands_seen\":" << stats.hands_seen << ",\"hands_vpip\":" << stats.hands_vpip
            << ",\"vpip_pct\":" << stats.vpip_pct << ",\"hands_pfr\":" << stats.hands_pfr
            << ",\"pfr_pct\":" << stats.pfr_pct << ",\"bets\":" << stats.bets
            << ",\"calls\":" << stats.calls << ",\"af\":" << stats.af
            << ",\"hands_won\":" << stats.hands_won << ",\"win_rate\":" << stats.win_rate
            << ",\"total_net\":" << stats.total_net
            << ",\"avg_bb_per_100\":" << stats.avg_bb_per_100 << "}";
          respBody = j.str();
        }
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /api/players/<id>/achievements — 玩家成就列表
      if (data.find("GET /api/players/") == 0 &&
          data.find("/achievements") != std::string::npos) {
        std::string respBody = "[]";
        if (g_db_initialized) {
          auto path_end = data.find(" ", 21);
          std::string path = data.substr(21, path_end - 21);
          auto slash = path.find("/achievements");
          int64_t pid = std::atoll(path.substr(0, slash).c_str());
          auto ach = DatabaseManager::Instance().GetAccountRepo().GetAchievements(pid);
          std::ostringstream j;
          j << "[";
          bool first = true;
          for (auto& a : ach) {
            if (!first) j << ",";
            first = false;
            j << "{\"id\":\"" << a.id << "\",\"name\":\"" << a.name << "\","
              << "\"desc\":\"" << a.desc << "\",\"icon\":\"" << a.icon << "\","
              << "\"unlocked\":" << (a.unlocked ? "true" : "false") << "}";
          }
          j << "]";
          respBody = j.str();
        }
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /api/leaderboard — top players
      if (data.find("GET /api/leaderboard") == 0) {
        std::string respBody = "[]";
        if (g_db_initialized) {
          int limit = 20;
          auto qpos = data.find("limit=");
          if (qpos != std::string::npos) {
            limit = std::atoi(data.c_str() + qpos + 7);
            if (limit <= 0 || limit > 100) limit = 20;
          }
          auto entries = DatabaseManager::Instance().GetPlayerRepo().GetLeaderboard(limit);
          std::ostringstream j;
          j << "[";
          for (size_t i = 0; i < entries.size(); ++i) {
            auto& [info, stats] = entries[i];
            if (i > 0) j << ",";
            j << "{\"player_id\":" << info.player_id << ",\"name\":\"" << info.name << "\""
              << ",\"display_name\":\"" << info.display_name << "\""
              << ",\"hands_played\":" << info.hands_played << ",\"vpip_pct\":" << stats.vpip_pct
              << ",\"pfr_pct\":" << stats.pfr_pct << ",\"af\":" << stats.af
              << ",\"win_rate\":" << stats.win_rate << ",\"total_net\":" << stats.total_net
              << ",\"avg_bb_per_100\":" << stats.avg_bb_per_100 << "}";
          }
          j << "]";
          respBody = j.str();
        }
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /api/tournaments - list tournaments
      if (data.find("GET /api/tournaments") == 0) {
        std::string respBody = g_tourn ? g_tourn->ListTournamentsJSON() : "[]";
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /api/tournaments/<id> - tournament state
      if (data.find("GET /api/tournaments/") == 0) {
        std::string respBody = "{}";
        if (g_tourn) {
          auto path_end = data.find(" ", 19);
          int tid = std::atoi(data.substr(19, path_end - 19).c_str());
          respBody = g_tourn->GetTournamentStateJSON(tid);
        }
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP POST /api/tournaments/join - join tournament
      if (data.find("POST /api/tournaments/join") == 0) {
        std::string respBody = "{\"result\":\"error\",\"message\":\"unauthorized\"}";
        if (g_tourn) {
          std::string body = parseHttpBody(data);
          int tid = static_cast<int>(jsonGetNum(body, "tournament_id"));
          int pid = static_cast<int>(jsonGetNum(body, "player_id"));
          int64_t verified_id = 0;
          if (!authorizeHttpPlayer(data, body, pid, verified_id)) {
            std::string resp = httpResponse(401, respBody);
            send(fd, resp.c_str(), resp.size(), 0);
            close(fd);
            it = clients.erase(it);
            continue;
          }
          pid = static_cast<int>(verified_id);
          std::string name = jsonGetStr(body, "name");
          if (name.empty()) {
            if (auto acct = g_auth->GetPlayer(verified_id)) name = acct->username;
          }
          auto entry_cost = g_tourn->GetTournamentEntryCost(tid);
          if (!entry_cost) {
            respBody = "{\"result\":\"error\",\"message\":\"tournament not found\"}";
          } else {
            std::string tourn_ref = "tournament:" + std::to_string(tid);
            int64_t cost = static_cast<int64_t>(*entry_cost);
            std::string ledger_err;
            if (!debitBuyIn(verified_id, cost, tourn_ref, ledger_err)) {
              respBody = "{\"result\":\"error\",\"message\":\"" + ledger_err + "\",\"code\":402}";
            } else {
              bool ok = g_tourn->JoinTournament(tid, pid, name);
              if (!ok) {
                refundBuyIn(verified_id, cost, tourn_ref, ledger_err);
                respBody = "{\"result\":\"error\",\"message\":\"join failed\"}";
              } else {
                respBody = "{\"result\":\"ok\",\"tournament_id\":" + std::to_string(tid) +
                           ",\"entry_cost\":" + std::to_string(cost) + "}";
              }
            }
          }
        }
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }
      

      // HTTP POST /api/tournaments/leave - unregister during registration (refund entry)
      if (data.find("POST /api/tournaments/leave") == 0) {
        std::string respBody = "{\"result\":\"error\",\"message\":\"unauthorized\"}";
        if (g_tourn) {
          std::string body = parseHttpBody(data);
          int tid = static_cast<int>(jsonGetNum(body, "tournament_id"));
          int pid = static_cast<int>(jsonGetNum(body, "player_id"));
          int64_t verified_id = 0;
          if (!authorizeHttpPlayer(data, body, pid, verified_id)) {
            std::string resp = httpResponse(401, respBody);
            send(fd, resp.c_str(), resp.size(), 0);
            close(fd);
            it = clients.erase(it);
            continue;
          }
          auto leave = g_tourn->LeaveTournament(tid, static_cast<int>(verified_id));
          if (!leave.success) {
            respBody = "{\"result\":\"error\",\"message\":\"" + leave.error + "\"}";
          } else {
            if (leave.refund_amount > 0) {
              std::string tourn_ref = "tournament:" + std::to_string(tid) + ":leave";
              std::string ledger_err;
              if (!refundBuyIn(verified_id, leave.refund_amount, tourn_ref, ledger_err)) {
                respBody = "{\"result\":\"error\",\"message\":\"" + ledger_err + "\"}";
              } else {
                respBody = "{\"result\":\"ok\",\"refund\":" + std::to_string(leave.refund_amount) + "}";
              }
            } else {
              respBody = "{\"result\":\"ok\",\"refund\":0}";
            }
          }
        }
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP POST /api/tournaments/rebuy - debit rebuy cost then process rebuy
      if (data.find("POST /api/tournaments/rebuy") == 0) {
        std::string respBody = "{\"result\":\"error\",\"message\":\"unauthorized\"}";
        if (g_tourn) {
          std::string body = parseHttpBody(data);
          int tid = static_cast<int>(jsonGetNum(body, "tournament_id"));
          int pid = static_cast<int>(jsonGetNum(body, "player_id"));
          int64_t verified_id = 0;
          if (!authorizeHttpPlayer(data, body, pid, verified_id)) {
            std::string resp = httpResponse(401, respBody);
            send(fd, resp.c_str(), resp.size(), 0);
            close(fd);
            it = clients.erase(it);
            continue;
          }
          auto rebuy_cost = g_tourn->GetTournamentRebuyCost(tid);
          if (!rebuy_cost) {
            respBody = "{\"result\":\"error\",\"message\":\"rebuy_unavailable\"}";
          } else {
            std::string tourn_ref = "tournament:" + std::to_string(tid) + ":rebuy";
            int64_t cost = static_cast<int64_t>(*rebuy_cost);
            std::string ledger_err;
            if (!debitBuyIn(verified_id, cost, tourn_ref, ledger_err)) {
              respBody = "{\"result\":\"error\",\"message\":\"" + ledger_err + "\",\"code\":402}";
            } else if (!g_tourn->Rebuy(tid, static_cast<int>(verified_id))) {
              refundBuyIn(verified_id, cost, tourn_ref, ledger_err);
              respBody = "{\"result\":\"error\",\"message\":\"rebuy_failed\"}";
            } else {
              respBody = "{\"result\":\"ok\",\"rebuy_cost\":" + std::to_string(cost) + "}";
            }
          }
        }
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP upgrade (table/ws paths only)

      // HTTP GET /api/admin/stats — server statistics
      if (data.find("GET /api/admin/stats") == 0) {
        if (!IsAdminRequest(data)) {
          std::string resp = httpResponse(401, "{\"error\":\"unauthorized\"}");
          send(fd, resp.c_str(), resp.size(), 0); close(fd); it = clients.erase(it); continue;
        }
        std::ostringstream j;
        j << "{";
        j << "\"tables\":" << (g_game ? g_game->TableCount() : 0);
        j << ",\"db_healthy\":" << (g_db_initialized && DatabaseManager::Instance().IsHealthy() ? "true" : "false");
        j << ",\"tournament_running\":" << (g_tourn ? "true" : "false");
        if (g_db_initialized) {
          auto dbStats = DatabaseManager::Instance().GetDatabaseStats();
          // count hands and players
          auto rs1 = DatabaseManager::Instance().GetDatabase().Query("SELECT COUNT(*) FROM hands");
          int hand_count = 0;
          if (rs1.Next()) hand_count = rs1.GetRow().GetInt(0);
          auto rs2 = DatabaseManager::Instance().GetDatabase().Query("SELECT COUNT(*) FROM players");
          int player_count = 0;
          if (rs2.Next()) player_count = rs2.GetRow().GetInt(0);
          auto rs3 = DatabaseManager::Instance().GetDatabase().Query("SELECT COUNT(*) FROM accounts");
          int account_count = 0;
          if (rs3.Next()) account_count = rs3.GetRow().GetInt(0);
          j << ",\"hand_count\":" << hand_count;
          j << ",\"player_count\":" << player_count;
          j << ",\"account_count\":" << account_count;
        }
        j << "}";
        std::string resp = httpResponse(200, j.str());
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }
      // HTTP GET /api/admin/players — all registered accounts
      if (data.find("GET /api/admin/players") == 0) {
        if (!IsAdminRequest(data)) {
          std::string resp = httpResponse(401, "{\"error\":\"unauthorized\"}");
          send(fd, resp.c_str(), resp.size(), 0); close(fd); it = clients.erase(it); continue;
        }
        std::string respBody = "[]";
        if (g_db_initialized) {
          std::ostringstream j;
          j << "[";
          auto rs = DatabaseManager::Instance().GetDatabase().Query(
              "SELECT id, username, display_name, chips, elo_rating, hands_played, created_at, last_login FROM accounts ORDER BY id");
          bool first = true;
          while (rs.Next()) {
            auto r = rs.GetRow();
            if (!first) j << ",";
            first = false;
            j << "{\"id\":" << r.GetInt64(0)
              << ",\"username\":\"" << r.GetString(1) << "\""
              << ",\"display_name\":\"" << r.GetString(2) << "\""
              << ",\"chips\":" << r.GetInt64(3)
              << ",\"elo\":" << r.GetInt64(4)
              << ",\"hands\":" << r.GetInt64(5)
              << ",\"created\":\"" << r.GetString(6) << "\""
              << ",\"last_login\":\"" << r.GetString(7) << "\"}";
          }
          j << "]";
          respBody = j.str();
        }
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }
      // HTTP GET /api/spectator/<id> — spectator tournament snapshot
      // POST /api/matchmaking/join — join matchmaking queue
      if (data.find("POST /api/matchmaking/join") == 0) {
        std::string respBody = "{\"result\":\"error\",\"message\":\"unauthorized\"}";
        std::string body = parseHttpBody(data);
        int64_t pid = static_cast<int64_t>(jsonGetNum(body, "player_id"));
        int64_t verified_id = 0;
        if (!authorizeHttpPlayer(data, body, pid, verified_id)) {
          std::string resp = httpResponse(401, respBody);
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        pid = verified_id;
        std::string name = jsonGetStr(body, "name");
        if (name.empty()) {
          if (auto acct = g_auth->GetPlayer(pid)) name = acct->username;
        }
        std::string game_type = jsonGetStr(body, "game_type");
        if (game_type.empty()) game_type = "nlhe";
        double buy_in = jsonGetNum(body, "buy_in");
        if (buy_in <= 0) buy_in = 200;
        respBody = JoinMatchmaking(pid, name, game_type, buy_in);
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }
      // POST /api/matchmaking/leave — leave matchmaking queue
      if (data.find("POST /api/matchmaking/leave") == 0) {
        std::string body = parseHttpBody(data);
        int64_t pid = static_cast<int64_t>(jsonGetNum(body, "player_id"));
        int64_t verified_id = 0;
        if (!authorizeHttpPlayer(data, body, pid, verified_id)) {
          std::string resp = httpResponse(401, "{\"result\":\"error\",\"message\":\"unauthorized\"}");
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        std::string respBody = LeaveMatchmaking(verified_id);
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }
      // GET /api/matchmaking/status — check queue status
      if (data.find("GET /api/matchmaking/status") == 0) {
        std::string respBody = "{\"in_queue\":false}";
        auto qpos = data.find("player_id=");
        if (qpos != std::string::npos) {
          int64_t pid = std::atoll(data.c_str() + qpos + 11);
          respBody = GetMatchmakingStatus(pid);
        }
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }
      if (data.find("GET /api/spectator/") == 0) {
        std::string respBody = "{}";
        if (g_tourn) {
          auto path_end = data.find(" ", 19);
          int tid = std::atoi(data.substr(19, path_end - 19).c_str());
          respBody = g_tourn->GetTournamentStateJSON(tid);
        }
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }
      if (data.find("GET /table") == 0 || data.find("GET /ws") == 0 || data.find("GET / ") == 0) {
        // Rate-limit WebSocket connections per IP.
        if (!poker_engine::base::RateLimiters::WSConnectionPerIP().TryConsume(peerIp(fd))) {
          std::string resp = httpResponse(429, "{\"error\":\"too many connections\"}");
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        // Validate Origin header to prevent Cross-Site WebSocket Hijacking.
        std::string origin = ExtractOriginHeader(data);
        if (!origin.empty() && !IsOriginAllowed(origin)) {
          std::string resp = httpResponse(403, "{\"error\":\"origin not allowed\"}", origin);
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }

        // Read the auth token from the WS subprotocol (browser-safe) or the
        // Authorization: Bearer header. Tokens are NEVER accepted from the URL
        // query string, so they cannot leak into proxy/edge access logs or
        // browser history.
        std::string wsToken, wsTableId;
        bool isSpectator = false;

        std::string header_token = poker_engine::cli::ExtractBearerToken(data);
        if (!header_token.empty()) {
          wsToken = header_token;
        }
        if (wsToken.empty()) {
          std::string proto_token = poker_engine::cli::ExtractWsProtocolToken(data);
          if (!proto_token.empty()) wsToken = proto_token;
        }

        // Parse table_id and spectator from URL query (non-secret parameters).
        auto qpos = data.find("?");
        if (qpos != std::string::npos) {
          auto eq = data.find(" ", qpos);
          std::string query = data.substr(qpos + 1, eq - qpos - 1);
          size_t pos = 0;
          while (pos < query.size()) {
            auto amp = query.find("&", pos);
            if (amp == std::string::npos) amp = query.size();
            auto eq2 = query.find("=", pos);
            if (eq2 != std::string::npos && eq2 < amp) {
              std::string key = query.substr(pos, eq2 - pos);
              std::string val = urlDecode(query.substr(eq2 + 1, amp - eq2 - 1));
              if (key == "table_id")
                wsTableId = val;
              else if (key == "spectator")
                isSpectator = (val == "1" || val == "true");
            }
            pos = amp + 1;
          }
        }

        // Verify token
        int32_t playerId = 0;
        std::string playerName, tableId = wsTableId.empty() ? "main" : wsTableId;
        if (!wsToken.empty()) {
          auto verified = g_auth->GetTokenService().Verify(wsToken);
          if (verified.has_value()) {
            playerId = (int32_t)verified.value();
            auto acct = g_auth->GetPlayer(verified.value());
            if (acct.has_value()) playerName = acct->username;
          }
        }

        std::string hs = wsHandshake(data);
        send(fd, hs.c_str(), hs.size(), 0);

        it->second.player_id = playerId;
        it->second.name = playerName;
        it->second.table_id = tableId;
        it->second.token = wsToken;
        it->second.is_spectator = isSpectator;
        if (playerId > 0 && g_session_store && g_session_store->IsRedisConnected()) {
          g_session_store->BindPlayerInstance(playerId);
        }

        std::cout << "[Server] WebSocket connected: fd=" << fd << " player=" << playerName << "("
                  << playerId << ")"
                  << " table=" << tableId << "\n";
        ++it;
        continue;
      }

      // HTTP OPTIONS (CORS preflight)
      if (data.find("OPTIONS ") == 0) {
        std::string resp = httpResponse(200, "{}");
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /api/account — wallet balance for logged-in player
      if (data.find("GET /api/account") == 0) {
        int64_t verified_id = 0;
        if (!authorizeHttpPlayer(data, "", 0, verified_id)) {
          std::string resp = httpResponse(401, "{\"message\":\"unauthorized\"}");
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        int64_t chips = getAccountChips(verified_id);
        std::string display_name;
        if (g_auth) {
          auto acct = g_auth->GetPlayer(verified_id);
          if (acct.has_value()) display_name = acct->display_name;
        }
        std::string respBody = "{" + jsonNum("player_id", verified_id) + "," +
                               jsonNum("chips", chips) + "," +
                               jsonStr("display_name", display_name) + "}";
        std::string resp = httpResponse(200, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /api/account/export — GDPR data export (Article 20 portability)
      if (data.find("GET /api/account/export") == 0) {
        int64_t verified_id = 0;
        if (!authorizeHttpPlayer(data, "", 0, verified_id)) {
          std::string resp = httpResponse(401, "{\"message\":\"unauthorized\"}");
          send(fd, resp.c_str(), resp.size(), 0); close(fd); it = clients.erase(it); continue;
        }
        auto acct = g_auth ? g_auth->GetPlayer(verified_id) : std::optional<PlayerAccount>{};
        std::ostringstream j;
        j << "{";
        // Account info
        if (acct.has_value()) {
          j << "\"player_id\":" << acct->id
            << ",\"username\":\"" << jsonEscape(acct->username) << "\""
            << ",\"display_name\":\"" << jsonEscape(acct->display_name) << "\""
            << ",\"chips\":" << acct->chips
            << ",\"elo_rating\":" << acct->elo_rating
            << ",\"hands_played\":" << acct->hands_played
            << ",\"total_profit\":" << acct->total_profit
            << ",\"created_at\":\"" << std::to_string(
                std::chrono::duration_cast<std::chrono::seconds>(
                    acct->created_at.time_since_epoch()).count()) << "\"";
        } else {
          j << "\"player_id\":" << verified_id;
        }
        // Hand history available via GET /api/hands?player_id=N
        j << ",\"recent_hands_url\":\"/api/hands?player_id=" << verified_id << "\"";
        // Transactions
        j << ",\"transactions\":[";
        // ChipLedger query for recent transactions
        try {
          bool first = true;
          auto tx_rs = ParamQuery(
              "SELECT id, amount, balance_after, type, ref_id, created_at "
              "FROM chip_ledger WHERE player_id = ? ORDER BY created_at DESC LIMIT 50",
              {{1, verified_id}});
          first = true;
          while (tx_rs.Next()) {
            auto row = tx_rs.GetRow();
            if (!first) j << ","; first = false;
            j << "{\"id\":" << row.GetInt64(0) << ",\"amount\":" << row.GetInt64(1)
              << ",\"balance\":" << row.GetInt64(2) << ",\"type\":\"" << jsonEscape(row.GetString(3))
              << "\",\"ref\":\"" << jsonEscape(row.GetString(4))
              << "\",\"time\":\"" << jsonEscape(row.GetString(5)) << "\"}";
          }
        } catch (...) {}
        j << "]";
        j << ",\"export_date\":\"" << todayStr() << "\"";
        j << "}";
        std::string resp = httpResponse(200, j.str());
        send(fd, resp.c_str(), resp.size(), 0); close(fd); it = clients.erase(it); continue;
      }

      // HTTP POST /api/account/delete — 隐私删除（GDPR/个人信息保护法）
      if (data.find("POST /api/account/delete") == 0) {
        int64_t verified_id = 0;
        if (!authorizeHttpPlayer(data, "", 0, verified_id)) {
          std::string resp = httpResponse(401, "{\"message\":\"unauthorized\"}");
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        bool ok = DatabaseManager::Instance().GetAccountRepo().DeleteAccount(verified_id);
        if (ok && g_auth) g_auth->EvictPlayer(verified_id);  // 使内存缓存失效
        std::string respBody = ok ? "{\"message\":\"account deleted\"}"
                                  : "{\"message\":\"delete failed\"}";
        std::string resp = httpResponse(ok ? 200 : 500, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP GET /api/daily-bonus — 查询今日是否可领及预览奖励
      if (data.find("GET /api/daily-bonus") == 0) {
        int64_t verified_id = 0;
        if (!authorizeHttpPlayer(data, "", 0, verified_id)) {
          std::string resp = httpResponse(401, "{\"message\":\"unauthorized\"}");
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        auto db = DatabaseManager::Instance().GetAccountRepo().GetDailyBonus(verified_id);
        std::string today = todayStr();
        bool can_claim = (db.last_claim != today);
        int64_t preview_streak =
            (db.last_claim == yesterdayStr()) ? db.streak + 1 : 1;
        int64_t reward = can_claim ? dailyReward(preview_streak) : 0;
        std::string body = "{" + jsonNum("player_id", verified_id) + "," +
                             jsonBool("can_claim", can_claim) + "," +
                             jsonNum("streak", db.streak) + "," +
                             jsonNum("reward", reward) + "}";
        std::string resp = httpResponse(200, body);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP POST /api/daily-bonus/claim — 领取每日奖励
      if (data.find("POST /api/daily-bonus/claim") == 0) {
        int64_t verified_id = 0;
        if (!authorizeHttpPlayer(data, "", 0, verified_id)) {
          std::string resp = httpResponse(401, "{\"message\":\"unauthorized\"}");
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        auto db = DatabaseManager::Instance().GetAccountRepo().GetDailyBonus(verified_id);
        std::string today = todayStr();
        if (db.last_claim == today) {
          std::string resp = httpResponse(400, "{\"message\":\"already claimed today\"}");
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        int64_t new_streak = (db.last_claim == yesterdayStr()) ? db.streak + 1 : 1;
        int64_t reward = dailyReward(new_streak);
        auto lr = DatabaseManager::Instance().GetChipLedger().CreditBonus(
            verified_id, reward, "daily_bonus");
        if (!lr.success) {
          std::string resp = httpResponse(500, "{\"message\":\"" + lr.error + "\"}");
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        syncAuthChips(verified_id, lr.balance_after);
        DatabaseManager::Instance().GetAccountRepo().UpdateDailyBonus(verified_id, today,
                                                                  new_streak);
        std::string body = "{" + jsonNum("player_id", verified_id) + "," +
                             jsonNum("reward", reward) + "," +
                             jsonNum("streak", new_streak) + "," +
                             jsonNum("balance_after", lr.balance_after) + "}";
        std::string resp = httpResponse(200, body);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // HTTP POST /api/auth/register
      if (data.find("POST /api/auth/register") == 0) {
        if (!poker_engine::base::RateLimiters::RegisterPerIP().TryConsume(peerIp(fd))) {
          std::string resp = httpResponse(429, "{\"message\":\"注册过于频繁，请稍后再试\"}");
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        std::string body = parseHttpBody(data);
        std::string username = jsonGetStr(body, "username");
        std::string password = jsonGetStr(body, "password");
        std::string displayName = jsonGetStr(body, "display_name");

        // 服务端输入校验：阻止畸形/恶意注册
        auto trimCopy = [](const std::string& s) {
          size_t a = 0, b = s.size();
          while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
          while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
          return s.substr(a, b - a);
        };
        username = trimCopy(username);
        if (username.empty() || username.size() > 20 ||
            !std::all_of(username.begin(), username.end(), [](unsigned char c) {
              return std::isalnum(c) || c == '_';
            })) {
          std::string resp =
              httpResponse(400, "{\"message\":\"用户名需为3-20位字母、数字或下划线\"}");
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        if (password.empty() || password.size() < 6 || password.size() > 64) {
          std::string resp = httpResponse(400, "{\"message\":\"密码需为6-64位\"}");
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        if (displayName.empty()) displayName = username;
        if (displayName.size() > 24) displayName = displayName.substr(0, 24);

        auto result = g_auth->Register(username, password, displayName);
        std::string respBody;
        if (result.success) {
          respBody = "{" + jsonStr("token", result.token) + "," +
                     jsonNum("player_id", result.player_id) + "," + jsonStr("username", username) +
                     "," + jsonStr("display_name", displayName) + "}";
        } else {
          respBody = "{" + jsonStr("message", result.error_message) + "}";
        }
        std::string resp = httpResponse(result.success ? 200 : 400, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        std::cout << "[HTTP] Register: " << username << " → " << (result.success ? "OK" : "FAIL")
                  << "\n";
        it = clients.erase(it);
        continue;
      }

      // HTTP POST /api/auth/login
      if (data.find("POST /api/auth/login") == 0) {
        if (!poker_engine::base::RateLimiters::LoginPerIP().TryConsume(peerIp(fd))) {
          std::string resp = httpResponse(429, "{\"message\":\"rate limit exceeded\"}");
          send(fd, resp.c_str(), resp.size(), 0);
          close(fd);
          it = clients.erase(it);
          continue;
        }
        std::string body = parseHttpBody(data);
        std::string username = jsonGetStr(body, "username");
        std::string password = jsonGetStr(body, "password");

        auto result = g_auth->Login(username, password);
        std::string respBody;
        if (result.success) {
          // Check ban list before issuing a token.
          if (IsPlayerBanned(result.player_id)) {
            respBody = "{\"message\":\"account suspended\"}";
            result.success = false;
          } else {
            auto acct = g_auth->GetPlayer(result.player_id);
            std::string displayName = acct.has_value() ? acct->display_name : username;
            respBody = "{" + jsonStr("token", result.token) + "," +
                       jsonNum("player_id", result.player_id) + "," + jsonStr("username", username) +
                       "," + jsonStr("display_name", displayName) + "}";
          }
        } else {
          respBody = "{" + jsonStr("message", result.error_message) + "}";
        }
        std::string resp = httpResponse(result.success ? 200 : 400, respBody);
        send(fd, resp.c_str(), resp.size(), 0);
        close(fd);
        std::cout << "[HTTP] Login: " << username << " → " << (result.success ? "OK" : "FAIL")
                  << "\n";
        it = clients.erase(it);
        continue;
      }

      // Close frame — send close back then close
      if (((uint8_t)data[0] & 0x0F) == 0x8) {
        std::cout << "[Server] Close frame received fd=" << fd << " player=" << it->second.name
                  << "\n";
        leaveCurrentTable(it->second);
        sendClose(fd);
        close(fd);
        it = clients.erase(it);
        continue;
      }

      // Ping → Pong
      if (((uint8_t)data[0] & 0x0F) == 0x9) {
        auto pong = wsPong();
        send(fd, pong.c_str(), pong.size(), 0);
        ++it;
        continue;
      }

      std::string msg = wsDecode(data);
      if (msg.empty()) {
        std::cout << "[Server] wsDecode returned empty (fd=" << fd << ") data.size=" << data.size()
                  << " data[0]=" << std::hex << (int)(uint8_t)data[0] << std::dec << "\n";
        ++it;
        continue;
      }

      std::cout << "[Server] Received fd=" << fd << " size=" << msg.size()
                << " type=" << jsonGetStr(msg, "type") << "\n";

      // Parse frontend message format: {"type":"...","seq":...,"payload":{...}}
      std::string type = jsonGetStr(msg, "type");
      std::string payloadStr = "{}";
      auto pp = msg.find("\"payload\"");
      if (pp != std::string::npos) {
        auto ps = msg.find("{", pp);
        if (ps != std::string::npos) {
          int depth = 0;
          size_t pe = ps;
          for (; pe < msg.size(); ++pe) {
            if (msg[pe] == '{')
              depth++;
            else if (msg[pe] == '}') {
              depth--;
              if (depth == 0) break;
            }
          }
          payloadStr = msg.substr(ps, pe - ps + 1);
        }
      }

      poker_engine::network::GlobalMetrics().ws_messages.fetch_add(1, std::memory_order_relaxed);

      std::string response;

      if (type == "join_table") {
        // User already authenticated via token in WS URL
        std::string tableId = jsonGetStr(payloadStr, "table_id");
        double buyIn = jsonGetNum(payloadStr, "buy_in");
        int seatIdx = -1;
        if (payloadStr.find("\"seat_index\"") != std::string::npos) {
          seatIdx = static_cast<int>(jsonGetNum(payloadStr, "seat_index"));
        }
        if (buyIn <= 0) buyIn = 200;
        if (tableId.empty()) tableId = "main";

        // Use authenticated player info from WS connection
        const auto& cl = it->second;
        if (cl.player_id <= 0) {
          response = makeMsg("error", "{\"code\":401,\"message\":\"not authenticated\"}");
          sendTo(fd, response);
          ++it;
          continue;
        }

        std::string ledger_err;
        if (!debitBuyIn(cl.player_id, static_cast<int64_t>(buyIn), tableId, ledger_err)) {
          response = makeMsg("error", "{\"code\":402,\"message\":\"" + ledger_err + "\"}");
          sendTo(fd, response);
          ++it;
          continue;
        }
        bool joined =
            g_game->JoinTable(cl.player_id, tableId, cl.name, seatIdx, (int64_t)buyIn, cl.token);
        if (!joined) {
          refundBuyIn(cl.player_id, static_cast<int64_t>(buyIn), tableId, ledger_err);
          std::cout << "[DEBUG] JoinTable failed pid=" << cl.player_id << " table=" << tableId
                    << " seat=" << seatIdx << " buy_in=" << buyIn << "\n";
          response = makeMsg("error", "{\"code\":400,\"message\":\"failed to join table\"}");
          sendTo(fd, response);
          ++it;
          continue;
        }
        it->second.table_id = tableId;

        int64_t balance_after_join = getAccountChips(cl.player_id);
        std::string payload = "{" + jsonStr("table_id", tableId) + "," +
                              jsonNum("player_id", cl.player_id) + "," +
                              jsonStr("player_name", cl.name) + "," + jsonNum("buy_in", buyIn) +
                              "," + jsonNum("seat_index", seatIdx) + "," +
                              jsonNum("balance_after", balance_after_join) + "}";
        response = makeMsg("player_joined", payload);
        sendTo(fd, response);

        // Send table state
        std::string stateJson = g_game->GetTableStateJSON(tableId, it->second.player_id);
        response = makeMsg("table_state", stateJson);
        sendTo(fd, response);

        // Replay recent chat history to the joining player
        {
          std::lock_guard<std::mutex> hlock(g_chat_mutex);
          auto itH = g_chat_history.find(tableId);
          if (itH != g_chat_history.end()) {
            for (const auto& histMsg : itH->second) {
              auto enc = wsEncode(histMsg);
              ::send(fd, enc.c_str(), enc.size(), MSG_NOSIGNAL);
            }
          }
        }

        ++it;
        continue;
      }

      if (type == "leave_table") {
        std::string tableId = jsonGetStr(payloadStr, "table_id");
        if (tableId.empty()) tableId = it->second.table_id;
        if (tableId.empty()) tableId = "main";

        bool left = false;
        int64_t cashed_out = 0;
        int64_t balance_after = 0;
        std::string ledger_err;
        if (it->second.player_id > 0) {
          left = leaveTableWithCashOut(it->second.player_id, tableId, cashed_out, balance_after, ledger_err);
          if (left && cashed_out > 0) {
            std::cout << "[Server] Cash-out pid=" << it->second.player_id << " amount=" << cashed_out
                      << " balance=" << balance_after << "\n";
          }
        }

        std::string payload = "{" + jsonStr("table_id", tableId) + "," +
                              jsonNum("player_id", it->second.player_id) + "," +
                              jsonStr("player_name", it->second.name) + "," +
                              jsonStr("result", left ? "ok" : "not_seated") + "," +
                              jsonNum("cashed_out", cashed_out) + "," +
                              jsonNum("balance_after", balance_after) + "}";
        response = makeMsg("player_left", payload);
        sendTo(fd, response);

        std::string stateJson = g_game->GetTableStateJSON(tableId, it->second.player_id);
        response = makeMsg("table_state", stateJson);
        sendTo(fd, response);
        ++it;
        continue;
      }

      if (type == "player_action") {
        if (it->second.player_id <= 0) {
          response = makeMsg("error", "{\"code\":401,\"message\":\"not authenticated\"}");
          sendTo(fd, response);
          ++it;
          continue;
        }
        // Record server-side arrival time for timing-based cheat detection.
        auto server_arrival = std::chrono::steady_clock::now();

        std::string action = jsonGetStr(payloadStr, "action");
        double amount = jsonGetNum(payloadStr, "amount");
        if (amount < 0) amount = 0;  // 客户端传入负数/NaN 时钳制
        std::string tableId = jsonGetStr(payloadStr, "table_id");
        if (tableId.empty()) tableId = it->second.table_id;
        if (tableId.empty()) tableId = "main";

        std::string result =
            g_game->OnPlayerAction(it->second.player_id, tableId, action, (int64_t)amount, 0);

        if (result != "ok") {
          response = makeMsg(
              "error", "{\"code\":400,\"message\":\"操作无效（可能还没轮到你，或本手未参与）\"}");
          sendTo(fd, response);
          std::string stateJson = g_game->GetTableStateJSON(tableId, it->second.player_id);
          response = makeMsg("table_state", stateJson);
          sendTo(fd, response);
          ++it;
          continue;
        }

        std::string payload = "{" + jsonStr("player", it->second.name) + "," +
                              jsonStr("action", action) + "," + jsonNum("amount", amount) + "," +
                              jsonNum("chips_remaining", 0) + "}";
        response = makeMsg("action_taken", payload);
        sendTo(fd, response);

        std::string stateJson = g_game->GetTableStateJSON(tableId, it->second.player_id);
        response = makeMsg("table_state", stateJson);
        sendTo(fd, response);

        // Submit timing data for anti-cheat analysis.
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            server_arrival.time_since_epoch()).count();
        {
          static std::unordered_map<int32_t, std::vector<int64_t>> g_action_timings;
          static std::mutex g_timing_mutex;
          std::lock_guard<std::mutex> lock(g_timing_mutex);
          auto& timings = g_action_timings[it->second.player_id];
          timings.push_back(elapsed_ms);
          if (timings.size() > 50) timings.erase(timings.begin());
          // Flag if stddev is too low (bot-like consistency).
          if (timings.size() >= 10) {
            double sum = 0, sq_sum = 0;
            for (auto t : timings) { sum += t; sq_sum += t * t; }
            double mean = sum / timings.size();
            double variance = sq_sum / timings.size() - mean * mean;
            double stddev = std::sqrt(std::max(0.0, variance));
            if (stddev < 200.0 && mean > 500.0) {  // very consistent + deliberate delay = bot
              std::cout << "[ANTICHEAT-TIMING] Suspicious timing: pid=" << it->second.player_id
                        << " mean=" << static_cast<int>(mean) << "ms stddev="
                        << static_cast<int>(stddev) << "ms samples=" << timings.size() << "\n";
            }
          }
        }

        runBotTurns(tableId);

        stateJson = g_game->GetTableStateJSON(tableId, it->second.player_id);
        response = makeMsg("table_state", stateJson);
        sendTo(fd, response);

        ++it;
        continue;
      }

      if (type == "add_bots") {
        if (g_production_mode) {
          response = makeMsg("error", "{\"code\":403,\"message\":\"add_bots disabled in production\"}");
          sendTo(fd, response);
          ++it;
          continue;
        }
        if (it->second.player_id <= 0) {
          response = makeMsg("error", "{\"code\":401,\"message\":\"not authenticated\"}");
          sendTo(fd, response);
          ++it;
          continue;
        }
        std::string tableId = jsonGetStr(payloadStr, "table_id");
        int count = (int)jsonGetNum(payloadStr, "count");
        double buyIn = jsonGetNum(payloadStr, "buy_in");
        if (count <= 0) {
          std::string payload =
              "{" + jsonStr("table_id", tableId) + "," + jsonNum("added", 0) + "}";
          response = makeMsg("bots_added", payload);
          sendTo(fd, response);
          ++it;
          continue;
        }
        if (count > 5) count = 5;
        if (buyIn <= 0) buyIn = 200;
        if (tableId.empty()) tableId = "main";

        int added = g_game->AddBots(tableId, count, (int64_t)buyIn);

        std::string payload =
            "{" + jsonStr("table_id", tableId) + "," + jsonNum("added", added) + "}";
        response = makeMsg("bots_added", payload);
        sendTo(fd, response);

        std::string stateJson = g_game->GetTableStateJSON(tableId, it->second.player_id);
        response = makeMsg("table_state", stateJson);
        sendTo(fd, response);
        ++it;
        continue;
      }

      if (type == "start_game") {
        if (it->second.player_id <= 0) {
          response = makeMsg("error", "{\"code\":401,\"message\":\"not authenticated\"}");
          sendTo(fd, response);
          ++it;
          continue;
        }
        std::string tableId = jsonGetStr(payloadStr, "table_id");
        if (tableId.empty()) tableId = "main";

        bool ok = g_game->StartGame(tableId);

        if (!ok) {
          response = makeMsg(
              "error",
              "{\"code\":400,\"message\":\"无法开始：牌局进行中或玩家不足，请等待本手结束\"}");
          sendTo(fd, response);
          ++it;
          continue;
        }

        runBotTurns(tableId);

        std::string payload =
            "{" + jsonStr("table_id", tableId) + "," + jsonStr("result", "ok") + "}";
        response = makeMsg("game_started", payload);
        sendTo(fd, response);

        std::string stateJson = g_game->GetTableStateJSON(tableId, it->second.player_id);
        response = makeMsg("table_state", stateJson);
        sendTo(fd, response);
        ++it;
        continue;
      }

      if (type == "subscribe") {
        std::string tableId = jsonGetStr(payloadStr, "table_id");
        if (tableId.empty()) tableId = it->second.table_id;
        if (tableId.empty()) tableId = "main";
        std::string stateJson = g_game->GetTableStateJSON(tableId, it->second.player_id);
        response = makeMsg("table_state", stateJson);
        sendTo(fd, response);
        ++it;
        continue;
      }

      if (type == "chat_message") {
        const auto& cl = it->second;
        if (cl.player_id <= 0) {
          response = makeMsg("error", "{\"code\":401,\"message\":\"not authenticated\"}");
          sendTo(fd, response);
          ++it;
          continue;
        }
        // 发言频率限制：防止公网刷屏/广告轰炸
        if (!poker_engine::base::RateLimiters::ChatPerPlayer().TryConsume(std::to_string(cl.player_id))) {
          response = makeMsg(
              "chat_ack", "{\"ok\":false,\"rate_limited\":true,\"message\":\"发言过于频繁，请稍后再试\"}");
          sendTo(fd, response);
          ++it;
          continue;
        }
        std::string tableId = jsonGetStr(payloadStr, "table_id");
        if (tableId.empty()) tableId = cl.table_id;
        if (tableId.empty()) tableId = "main";
        std::string raw = sanitizeChat(jsonGetStr(payloadStr, "message"));
        raw = filterProfanity(raw);  // 敏感词/外链屏蔽
        if (raw.empty()) {
          ++it;
          continue;
        }
        std::string chatJson = makeMsg(
            "chat_message", "{" + jsonStr("table_id", tableId) + "," +
                               jsonNum("player_id", cl.player_id) + "," +
                               jsonStr("player_name", cl.name) + "," + jsonStr("message", raw) + "," +
                               std::string("\"timestamp\":") + std::to_string(nowMs()) + "}");
        // Persist to rolling history
        {
          std::lock_guard<std::mutex> hlock(g_chat_mutex);
          auto& hist = g_chat_history[tableId];
          hist.push_back(chatJson);
          while (hist.size() > kChatHistoryMax) hist.pop_front();
        }
        broadcastChat(tableId, chatJson);
        std::cerr << "[DBG] chat pid=" << cl.player_id << " table=" << tableId
                  << " raw='" << raw << "'\n";
        // echo ack to sender
        response = makeMsg("chat_ack", "{\"ok\":true}");
        sendTo(fd, response);
        ++it;
        continue;
      }

      if (type == "heartbeat") {
        response = makeMsg("heartbeat_ack", "{}");
        sendTo(fd, response);
        ++it;
        continue;
      }

      // Unknown → error
      response = makeMsg("error", "{\"code\":400,\"message\":\"unknown type: " + type + "\"}");
      sendTo(fd, response);
      ++it;
    }

    // Process bot actions for all tables (loop until human turn)
    for (int i = 0; i < 10; ++i) {
      std::string stateMain = g_game->GetTableStateJSON("main");
      // Check if current player is a bot (negative id)
      bool mainBotTurn = false;
      auto cpPos = stateMain.find("\"current_player_id\":");
      if (cpPos != std::string::npos) {
        int cpId = 0;
        auto numStart = stateMain.find_first_of("-0123456789", cpPos + 20);
        if (numStart != std::string::npos) cpId = std::atoi(stateMain.c_str() + numStart);
        if (cpId < 0) mainBotTurn = true;
      }
      if (!mainBotTurn) break;
      g_game->ProcessBotActions("main");
    }
    for (int i = 0; i < 10; ++i) {
      std::string stateT1 = g_game->GetTableStateJSON("table_1");
      bool t1BotTurn = false;
      auto cpPos = stateT1.find("\"current_player_id\":");
      if (cpPos != std::string::npos) {
        int cpId = 0;
        auto numStart = stateT1.find_first_of("-0123456789", cpPos + 20);
        if (numStart != std::string::npos) cpId = std::atoi(stateT1.c_str() + numStart);
        if (cpId < 0) t1BotTurn = true;
      }
      if (!t1BotTurn) break;
      g_game->ProcessBotActions("table_1");
    }
  }

  if (g_redis_heartbeat_thread.joinable()) g_redis_heartbeat_thread.join();
  if (g_session_store) g_session_store->Disconnect();
  delete g_tourn;
  delete g_game;
  delete g_auth;
  close(server_fd);
  std::cout << "[Server] Shutdown complete.\n";
  return 0;
}
