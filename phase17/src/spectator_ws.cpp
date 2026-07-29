#include "poker_engine/spectator/spectator_ws.h"

#include <nlohmann/json.hpp>

#include "poker_engine/base/logging.h"

namespace poker_engine::spectator {

SpectatorWSServer::SpectatorWSServer(uint16_t port, network::WSServer& ws_server,
                                     SpectatorManager& spec_mgr)
    : port_(port), ws_server_(ws_server), spec_mgr_(spec_mgr) {}

SpectatorWSServer::~SpectatorWSServer() { Stop(); }

bool SpectatorWSServer::Start() {
  // Register message handler on the shared WSServer
  // Op code 2 = table state push (reused for spectator events)
  ws_server_.RegisterHandler(2, [this](network::WSSession::Ptr session, const nlohmann::json& msg) {
    HandleMessage(session, msg);
  });

  ws_server_.SetCloseHandler([this](int64_t session_id, int64_t player_id) {
    HandleDisconnection(session_id, player_id);
  });

  running_ = true;
  broadcast_thread_ = std::thread(&SpectatorWSServer::BroadcastLoop, this);
  PE_LOG_INFO("Spectator WS service started (using shared WSServer on port {})", port_);
  return true;
}

void SpectatorWSServer::Stop() {
  running_ = false;
  if (broadcast_thread_.joinable()) {
    broadcast_thread_.join();
  }
  PE_LOG_INFO("Spectator WS service stopped");
}

int SpectatorWSServer::SpectatorCount() const { return spec_mgr_.GetStats().active_sessions; }

void SpectatorWSServer::HandleConnection(network::WSSession::Ptr session) {
  if (!session) return;

  int64_t sid = session->session_id();
  int64_t pid = session->player_id();

  PE_LOG_INFO("Spectator connected: session={}, player={}", sid, pid);

  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    session_tournament_[sid] = 0;  // not subscribed yet
  }
}

void SpectatorWSServer::HandleMessage(network::WSSession::Ptr session, const nlohmann::json& msg) {
  if (!session) return;

  try {
    std::string type = msg.value("type", "");
    int64_t sid = session->session_id();

    if (type == "subscribe") {
      uint64_t tid = msg.value("tournament_id", 0ULL);
      if (tid > 0) {
        {
          std::lock_guard<std::mutex> lock(session_mutex_);
          session_tournament_[sid] = tid;
        }
        spec_mgr_.Subscribe(sid, tid);

        auto evt = spec_mgr_.CreateTournamentStateEvent(tid);
        SendToSession(sid, evt);

        PE_LOG_INFO("Session {} subscribed to tournament {}", sid, tid);
      }
    } else if (type == "unsubscribe") {
      spec_mgr_.Unsubscribe(sid);
      {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_tournament_[sid] = 0;
      }
    } else if (type == "chat") {
      std::string message = msg.value("message", "");
      if (!message.empty()) {
        spec_mgr_.HandleChat(sid, message);
      }
    } else if (type == "request_history") {
      uint64_t tid = msg.value("tournament_id", 0ULL);
      uint64_t hid = msg.value("hand_id", 0ULL);
      int limit = msg.value("limit", 50);

      auto history = spec_mgr_.GetHandHistory(tid, hid, limit);
      for (auto& evt : history) {
        SendToSession(sid, evt);
      }
    } else if (type == "heartbeat") {
      session->Touch();
      nlohmann::json ack = {{"type", "heartbeat_ack"}, {"timestamp", msg.value("timestamp", 0)}};
      session->Send(ack);
    }
  } catch (const std::exception& e) {
    PE_LOG_WARN("SpectatorWSServer: error handling message: {}", e.what());
  }
}

void SpectatorWSServer::HandleDisconnection(int64_t session_id, int64_t player_id) {
  uint64_t tid = 0;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    auto it = session_tournament_.find(session_id);
    if (it != session_tournament_.end()) {
      tid = it->second;
      session_tournament_.erase(it);
    }
  }

  if (tid > 0) {
    spec_mgr_.Unsubscribe(session_id);
    PE_LOG_INFO("Session {} disconnected from tournament {}", session_id, tid);
  }
}

void SpectatorWSServer::SendToSession(int64_t session_id, const SpectatorEvent& evt) {
  try {
    nlohmann::json j;
    j["type"] = "spectator_event";
    j["data"] = nlohmann::json::parse(evt.Serialize());
    ws_server_.SendTo(session_id, j);
  } catch (...) {
    // session may have been closed
  }
}

void SpectatorWSServer::BroadcastLoop() {
  using namespace std::chrono;

  while (running_) {
    std::this_thread::sleep_for(milliseconds(10));
  }
}

}  // namespace poker_engine::spectator
