#pragma once

#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "poker_engine/network/ws_server.h"
#include "poker_engine/network/ws_session.h"
#include "spectator_manager.h"
#include "spectator_types.h"

namespace poker_engine::spectator {

// ==================== WebSocket 观战服务端 ====================

class SpectatorWSServer {
 public:
  explicit SpectatorWSServer(uint16_t port, network::WSServer& ws_server,
                             SpectatorManager& spec_mgr);
  ~SpectatorWSServer();

  bool Start();
  void Stop();

  int SpectatorCount() const;

 private:
  void HandleMessage(network::WSSession::Ptr session, const nlohmann::json& msg);
  void HandleConnection(network::WSSession::Ptr session);
  void HandleDisconnection(int64_t session_id, int64_t player_id);
  void BroadcastLoop();

  uint16_t port_;
  network::WSServer& ws_server_;
  SpectatorManager& spec_mgr_;
  std::atomic<bool> running_{false};
  std::thread broadcast_thread_;
  mutable std::mutex hub_mutex_;

  // 序列号生成
  std::atomic<uint64_t> sequence_counter_{0};

  void SendToSession(int64_t session_id, const SpectatorEvent& evt);

  // session_id -> subscribed tournament
  std::unordered_map<int64_t, uint64_t> session_tournament_;
  std::mutex session_mutex_;
};

}  // namespace poker_engine::spectator
