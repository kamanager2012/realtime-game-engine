#include "poker_engine/network/websocket_server.h"

#include <httplib.h>

#include <cstring>
#include <iostream>

#include "poker_engine/base/logging.h"

namespace poker_engine::network {

WebSocketServer::WebSocketServer(int port) : port_(port) {}

WebSocketServer::~WebSocketServer() { Stop(); }

bool WebSocketServer::Start() {
  server_ = std::make_unique<httplib::Server>();

  // Health check endpoint
  server_->Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("{\"status\":\"ok\",\"type\":\"poker_ws\"}", "application/json");
  });

  // Table listing endpoint (HTTP fallback)
  server_->Get("/tables", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("[]", "application/json");
  });

  // WebSocket endpoint — this is the core
  server_->WebSocket("/ws", [this](const httplib::Request& req, httplib::ws::WebSocket& ws) {
    int cid = next_client_id_.fetch_add(1);

    // Register client
    auto conn = std::make_unique<ClientConn>();
    conn->info.id = cid;
    conn->info.ip = req.remote_addr;
    conn->ws_ptr = &ws;
    conn->alive = true;

    {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      clients_[cid] = std::move(conn);
    }

    PE_LOG_INFO("WS client {} connected from {}", cid, req.remote_addr);
    if (on_connect_) on_connect_(cid);

    // Reader loop: blocks until ws is closed.
    // cpp-httplib runs the WS handler on the server's accept thread,
    // so we read inline here and dispatch callbacks.
    std::string msg;
    while (true) {
      auto result = ws.read(msg);
      if (result == httplib::ws::ReadResult::Fail) {
        break;
      }
      // Text or Binary — both are valid messages
      if (on_message_) {
        on_message_(cid, msg);
      }
      msg.clear();
    }

    // Cleanup
    {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      clients_.erase(cid);
    }
    {
      std::lock_guard<std::mutex> lock(table_mutex_);
      auto it = client_to_table_.find(cid);
      if (it != client_to_table_.end()) {
        std::string tid = it->second;
        client_to_table_.erase(it);
        auto tit = table_to_clients_.find(tid);
        if (tit != table_to_clients_.end()) {
          tit->second.erase(std::remove(tit->second.begin(), tit->second.end(), cid),
                            tit->second.end());
        }
      }
    }

    PE_LOG_INFO("WS client {} disconnected", cid);
    if (on_disconnect_) on_disconnect_(cid);
  });

  // Start listening in a background thread
  server_thread_ = std::thread([this]() {
    if (!server_->listen("0.0.0.0", port_)) {
      PE_LOG_ERROR("Failed to listen on port {}", port_);
      running_ = false;
    }
  });

  // Give the server a moment to bind
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  running_ = true;
  PE_LOG_INFO("WebSocket server listening on port {}", port_);
  return true;
}

void WebSocketServer::Stop() {
  if (!running_) return;
  running_ = false;

  if (server_) {
    server_->stop();
  }
  if (server_thread_.joinable()) {
    server_thread_.join();
  }
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.clear();
  }
  PE_LOG_INFO("WebSocket server stopped");
}

bool WebSocketServer::IsRunning() const { return running_; }

int WebSocketServer::ConnectedClients() const {
  std::lock_guard<std::mutex> lock(clients_mutex_);
  return static_cast<int>(clients_.size());
}

void WebSocketServer::OnMessage(MessageHandler handler) { on_message_ = std::move(handler); }

void WebSocketServer::OnConnect(ConnectHandler handler) { on_connect_ = std::move(handler); }

void WebSocketServer::OnDisconnect(DisconnectHandler handler) {
  on_disconnect_ = std::move(handler);
}

void WebSocketServer::SendTo(int client_id, const std::string& msg) {
  std::lock_guard<std::mutex> lock(clients_mutex_);
  auto it = clients_.find(client_id);
  if (it == clients_.end()) return;
  auto* ws = static_cast<httplib::ws::WebSocket*>(it->second->ws_ptr);
  if (ws && it->second->alive) {
    ws->send(msg);
  }
}

void WebSocketServer::Broadcast(const std::string& msg) {
  std::lock_guard<std::mutex> lock(clients_mutex_);
  for (auto& [id, conn] : clients_) {
    auto* ws = static_cast<httplib::ws::WebSocket*>(conn->ws_ptr);
    if (ws && conn->alive) {
      ws->send(msg);
    }
  }
}

void WebSocketServer::BroadcastToTable(const std::string& table_id, const std::string& msg) {
  std::vector<int> client_ids;
  {
    std::lock_guard<std::mutex> lock(table_mutex_);
    auto it = table_to_clients_.find(table_id);
    if (it != table_to_clients_.end()) {
      client_ids = it->second;
    }
  }
  for (int cid : client_ids) {
    SendTo(cid, msg);
  }
}

void WebSocketServer::CreateTable(const std::string& table_id, const std::string& /*config_json*/) {
  std::lock_guard<std::mutex> lock(table_mutex_);
  table_to_clients_[table_id] = {};
}

void WebSocketServer::CloseTable(const std::string& table_id) {
  std::lock_guard<std::mutex> lock(table_mutex_);
  auto it = table_to_clients_.find(table_id);
  if (it != table_to_clients_.end()) {
    for (int cid : it->second) {
      client_to_table_.erase(cid);
    }
    table_to_clients_.erase(it);
  }
}

}  // namespace poker_engine::network
