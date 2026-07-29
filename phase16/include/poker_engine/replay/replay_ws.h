#pragma once

#include <unordered_map>

#include "poker_engine/base/serialization.h"
#include "poker_engine/network/websocket_server.h"
#include "replay_engine.h"

namespace poker_engine::replay {

class ReplayWSAdapter : public ReplayObserver {
 public:
  explicit ReplayWSAdapter(network::WebSocketServer* ws_server, persistence::DatabaseManager& db);

  bool StartStream(int64_t hand_id, const ReplayConfig& config, const std::string& client_token);
  void StopStream(const std::string& client_token);

  void OnEvent(const ReplayEvent& event) override;
  void OnSnapshot(const HandSnapshot& snapshot) override;
  void OnComplete(int64_t hand_id) override;
  void OnError(const std::string& error) override;

  bool IsStreaming(const std::string& client_token) const;

 private:
  void SendEvent(const std::string& token, const ReplayEvent& event);
  void SendSnapshot(const std::string& token, const HandSnapshot& snapshot);

  network::WebSocketServer* ws_server_;
  ReplayEngine engine_;

  std::unordered_map<std::string, ReplayConfig> active_streams_;
  std::mutex streams_mutex_;
};

namespace replay_ws {

enum class ReplayMessageType : uint8_t {
  HandList = 0,
  HandSummary = 1,
  ReplayStart = 2,
  ReplayEvent = 3,
  ReplaySnapshot = 4,
  ReplayEnd = 5,
  ReplayError = 6,
  ReplayControl = 7,
  AntiCheatReport = 8,
};

struct ReplayRequest {
  ReplayMessageType type;
  std::string payload;

  std::string Serialize() const;
  static std::optional<ReplayRequest> Deserialize(const std::string& json);
};

}  // namespace replay_ws

}  // namespace poker_engine::replay
