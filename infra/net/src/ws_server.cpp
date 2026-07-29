#include "poker_engine/network/ws_server.h"

#include <httplib.h>

#include <iostream>
#include <sstream>
#include <thread>

#include "poker_engine/base/logging.h"
#include "poker_engine/network/health_probe.h"
#include "poker_engine/network/ws_session.h"

namespace poker_engine::network {

int64_t WSServer::next_session_id_ = 1;

WSServer::WSServer(WSServerConfig config) : config_(std::move(config)) {}

WSServer::~WSServer() { Stop(); }

bool WSServer::Start() {
  server_ = std::make_unique<httplib::Server>();

  // ========== Health check ==========
  server_->Get("/health", [](const httplib::Request&, httplib::Response& res) {
    HealthSnapshot snapshot;
    res.set_content(BuildHealthJson(snapshot), "application/json");
  });

  // ========== HTTP POST: join (fallback without WS) ==========
  server_->Post("/api/join", [this](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json response;
    try {
      auto body = nlohmann::json::parse(req.body);
      std::string token = body.value("token", "");
      int64_t table_id = body.value("table_id", -1);

      int64_t player_id = 0;
      try {
        player_id = std::stoll(token);
      } catch (...) {
        response["error"] = "Invalid token";
        res.status = 401;
        res.set_content(response.dump(), "application/json");
        return;
      }

      response["status"] = "joined";
      response["player_id"] = player_id;
      response["table_id"] = table_id;
      res.set_content(response.dump(), "application/json");

    } catch (const std::exception& e) {
      response["error"] = e.what();
      res.status = 400;
      res.set_content(response.dump(), "application/json");
    }
  });

  // ========== WebSocket endpoint ==========
  server_->WebSocket(
      config_.ws_path.c_str(), [this](const httplib::Request& req, httplib::ws::WebSocket& ws) {
        // Extract token from query string
        std::string token = req.get_param_value("token");
        int64_t player_id = 0;
        if (!token.empty()) {
          try {
            player_id = std::stoll(token);
          } catch (...) {
          }
        }

        // Create session
        auto session = CreateSession(player_id, token);
        session->set_native_handle(&ws);

        {
          std::lock_guard<std::mutex> lock(handle_mutex_);
          handle_map_[&ws] = session;
        }

        PE_LOG_INFO("WS connection: session={}, player={}, from={}", session->session_id(),
                    player_id, req.remote_addr);

        // Reader loop — blocks until ws is closed
        std::string msg;
        while (true) {
          auto result = ws.read(msg);
          if (result == httplib::ws::ReadResult::Fail) {
            break;
          }
          // Dispatch message
          session->Touch();
          try {
            auto j = nlohmann::json::parse(msg);
            int op = j.value("op", -1);
            auto it = handlers_.find(op);
            if (it != handlers_.end()) {
              it->second(session, j);
            } else {
              PE_LOG_WARN("Unknown op={} from session={}", op, session->session_id());
            }
          } catch (const nlohmann::json::parse_error& e) {
            PE_LOG_ERROR("JSON parse error from session={}: {}", session->session_id(), e.what());
            nlohmann::json err;
            err["op"] = protocol::OP_ERROR;
            err["data"]["code"] = 1001;
            err["data"]["message"] = "Invalid JSON";
            session->Send(err);
          }
          msg.clear();
        }

        // Cleanup on disconnect
        session->Close();

        {
          std::lock_guard<std::mutex> lock(handle_mutex_);
          handle_map_.erase(&ws);
        }

        int64_t sid = session->session_id();
        int64_t pid = session->player_id();
        int64_t tid = session->table_id();

        if (tid >= 0) {
          std::lock_guard<std::mutex> lock(table_mutex_);
          auto it = table_sessions_.find(tid);
          if (it != table_sessions_.end()) {
            it->second.erase(sid);
            if (it->second.empty()) table_sessions_.erase(it);
          }
        }

        if (close_handler_) {
          close_handler_(sid, pid);
        }

        {
          std::lock_guard<std::mutex> lock(sessions_mutex_);
          sessions_.erase(sid);
        }

        PE_LOG_INFO("Session {} disconnected (player={}, table={})", sid, pid, tid);
      });

  // Start listening in background thread
  running_ = true;
  server_thread_ = std::thread([this]() {
    PE_LOG_INFO("WS Server starting on {}:{}", config_.bind_address, config_.port);
    if (!server_->listen(config_.bind_address.c_str(), config_.port)) {
      PE_LOG_ERROR("WS Server failed to listen on port {}", config_.port);
      running_ = false;
    }
  });

  // Heartbeat thread
  heartbeat_thread_ = std::thread([this]() { HeartbeatLoop(); });

  // Give server a moment to bind
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return true;
}

void WSServer::Stop() {
  if (!running_) return;
  running_ = false;

  if (heartbeat_thread_.joinable()) {
    heartbeat_thread_.join();
  }

  if (server_) {
    server_->stop();
  }
  if (server_thread_.joinable()) {
    server_thread_.join();
  }

  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.clear();
  }

  PE_LOG_INFO("WS Server stopped");
}

void WSServer::RegisterHandler(int op, WSHandler handler) { handlers_[op] = std::move(handler); }

void WSServer::SetCloseHandler(CloseHandler handler) { close_handler_ = std::move(handler); }

bool WSServer::SendTo(int64_t session_id, const nlohmann::json& msg) {
  auto session = GetSession(session_id);
  if (!session) return false;
  session->Send(msg);
  return true;
}

void WSServer::BroadcastToTable(int64_t table_id, const nlohmann::json& msg) {
  std::vector<int64_t> sids;
  {
    std::lock_guard<std::mutex> lock(table_mutex_);
    auto it = table_sessions_.find(table_id);
    if (it != table_sessions_.end()) {
      for (auto sid : it->second) sids.push_back(sid);
    }
  }
  for (auto sid : sids) {
    SendTo(sid, msg);
  }
}

size_t WSServer::ConnectionCount() const {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  return sessions_.size();
}

WSSession::Ptr WSServer::GetSession(int64_t session_id) const {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  auto it = sessions_.find(session_id);
  return it != sessions_.end() ? it->second : nullptr;
}

WSSession::Ptr WSServer::CreateSession(int64_t player_id, const std::string& token) {
  int64_t sid = next_session_id_++;
  auto session = std::make_shared<WSSession>(sid, player_id, token);

  std::lock_guard<std::mutex> lock(sessions_mutex_);
  sessions_[sid] = session;

  return session;
}

WSSession::Ptr WSServer::FindSessionByHandle(void* handle) const {
  std::lock_guard<std::mutex> lock(handle_mutex_);
  auto it = handle_map_.find(handle);
  return it != handle_map_.end() ? it->second : nullptr;
}

void WSServer::HeartbeatLoop() {
  while (running_) {
    std::this_thread::sleep_for(std::chrono::seconds(30));

    std::vector<int64_t> dead_sessions;

    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      for (auto& [sid, session] : sessions_) {
        if (!session->is_alive()) {
          dead_sessions.push_back(sid);
        } else if (session->IsTimeout(config_.ping_interval_sec * 3)) {
          PE_LOG_WARN("Session {} heartbeat timeout", sid);
          dead_sessions.push_back(sid);
        }
      }
    }

    for (int64_t sid : dead_sessions) {
      auto session = GetSession(sid);
      if (session) {
        session->Close();
      }
    }

    // Send heartbeat to alive sessions
    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
      for (auto& [sid, session] : sessions_) {
        if (session->is_alive()) {
          nlohmann::json ping;
          ping["op"] = protocol::OP_PING;
          ping["ts"] = now;
          session->Send(ping);
        }
      }
    }
  }
}

}  // namespace poker_engine::network
