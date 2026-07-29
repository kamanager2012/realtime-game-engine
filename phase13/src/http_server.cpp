#include <uWebSockets/src/App.h>

#include <functional>
#include <nlohmann/json.hpp>
#include <sstream>

#include "poker_engine/base/logging.h"
#include "poker_engine/network/auth_service.h"
#include "poker_engine/network/game_server.h"

namespace poker_engine::network {

using json = nlohmann::json;
using HttpResponse = uWS::HttpResponse<false>;
using HttpRequest = uWS::HttpRequest;

class HttpServer {
 public:
  HttpServer(uint16_t port, AuthService& auth, GameServer& gs)
      : port_(port), auth_(auth), game_server_(gs) {}

  void Start() {
    app_.post("/api/auth/register",
              [this](HttpResponse* res, HttpRequest* req) { HandleRegister(res, req); });

    app_.post("/api/auth/login",
              [this](HttpResponse* res, HttpRequest* req) { HandleLogin(res, req); });

    app_.get("/api/leaderboard",
             [this](HttpResponse* res, HttpRequest* req) { HandleLeaderboard(res, req); });

    app_.listen("0.0.0.0", port_, [this](us_listen_socket_t* token) {
      if (token) {
        LOG_INFO("HTTP server listening on port {}", port_);
      }
    });

    app_.run();
  }

 private:
  uWS::App app_;
  uint16_t port_;
  AuthService& auth_;
  GameServer& game_server_;

  void HandleRegister(HttpResponse* res, HttpRequest* req) {
    std::string body;
    res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;

      auto j = json::parse(body, nullptr, false);
      if (j.is_discarded()) {
        SendJson(res, 400, {{"success", false}, {"message", "Invalid JSON"}});
        return;
      }

      std::string username = j.value("username", "");
      std::string password = j.value("password", "");
      std::string display_name = j.value("display_name", "");

      auto result = auth_.Register(username, password, display_name);

      if (result.success) {
        SendJson(res, 200,
                 {{"success", true},
                  {"token", result.token},
                  {"player_id", result.player_id},
                  {"username", username}});
      } else {
        SendJson(res, 400, {{"success", false}, {"message", result.error_message}});
      }
    });
  }

  void HandleLogin(HttpResponse* res, HttpRequest* req) {
    std::string body;
    res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;

      auto j = json::parse(body, nullptr, false);
      if (j.is_discarded()) {
        SendJson(res, 400, {{"success", false}, {"message", "Invalid JSON"}});
        return;
      }

      std::string username = j.value("username", "");
      std::string password = j.value("password", "");

      auto result = auth_.Login(username, password);

      if (result.success) {
        SendJson(res, 200,
                 {{"success", true},
                  {"token", result.token},
                  {"player_id", result.player_id},
                  {"username", username}});
      } else {
        SendJson(res, 401, {{"success", false}, {"message", result.error_message}});
      }
    });
  }

  void HandleLeaderboard(HttpResponse* res, HttpRequest* req) {
    std::string auth_header = std::string(req->getHeader("authorization"));
    if (auth_header.empty()) {
      SendJson(res, 401, {{"error", "Missing authorization token"}});
      return;
    }

    std::string token = auth_header;
    if (token.rfind("Bearer ", 0) == 0) {
      token = token.substr(7);
    }

    auto player_id = auth_.GetTokenService().Verify(token);
    if (!player_id.has_value()) {
      SendJson(res, 401, {{"error", "Invalid token"}});
      return;
    }

    auto json_str = game_server_.GetLeaderboardJSON(50);
    SendJson(res, 200, {{"success", true}, {"leaderboard", json::parse(json_str, nullptr, false)}});
  }

  static void SendJson(HttpResponse* res, int status, const json& data) {
    std::string body = data.dump();
    res->writeStatus(std::to_string(status));
    res->writeHeader("Content-Type", "application/json");
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->end(body);
  }
};

}  // namespace poker_engine::network
