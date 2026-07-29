#include "poker_engine/replay/replay_ws.h"

#include <nlohmann/json.hpp>

#include "poker_engine/base/logging.h"

namespace poker_engine::replay {

ReplayWSAdapter::ReplayWSAdapter(network::WebSocketServer* ws_server,
                                 persistence::DatabaseManager& db)
    : ws_server_(ws_server), engine_(db) {}

bool ReplayWSAdapter::StartStream(int64_t hand_id, const ReplayConfig& config,
                                  const std::string& client_token) {
  std::lock_guard<std::mutex> lock(streams_mutex_);
  if (active_streams_.count(client_token)) return false;
  active_streams_[client_token] = config;
  engine_.StartReplay(hand_id, this, config);
  PE_LOG_INFO("Replay stream started for client {}, hand_id={}", client_token, hand_id);
  return true;
}

void ReplayWSAdapter::StopStream(const std::string& client_token) {
  std::lock_guard<std::mutex> lock(streams_mutex_);
  active_streams_.erase(client_token);
  engine_.Stop();
}

void ReplayWSAdapter::OnEvent(const ReplayEvent& event) {
  std::lock_guard<std::mutex> lock(streams_mutex_);
  nlohmann::json msg;
  msg["type"] = "action";
  msg["hand_id"] = event.hand_id;
  msg["player_id"] = event.player_id;
  msg["timestamp"] = event.timestamp;
  msg["details"] = nlohmann::json::parse(event.details);
  msg["seq"] = static_cast<uint64_t>(event.sequence_id);
  std::string json_str = msg.dump();
  for (auto& [token, config] : active_streams_) { /* ws_server_->Send(token, json_str); */
  }
}

void ReplayWSAdapter::OnSnapshot(const HandSnapshot& snapshot) {
  nlohmann::json msg;
  msg["type"] = "snapshot";
  msg["data"]["hand_id"] = snapshot.hand_id;
  msg["data"]["phase"] = snapshot.phase;
  msg["data"]["total_pot"] = snapshot.total_pot;
  std::string json_str = msg.dump();
  for (auto& [token, config] : active_streams_) { /* broadcast */
  }
}

void ReplayWSAdapter::OnComplete(int64_t hand_id) {
  nlohmann::json msg;
  msg["type"] = "hand_complete";
  msg["hand_id"] = hand_id;
  std::lock_guard<std::mutex> lock(streams_mutex_);
  for (auto it = active_streams_.begin(); it != active_streams_.end();)
    it = active_streams_.erase(it);
  PE_LOG_INFO("Replay complete for hand {}", hand_id);
}

void ReplayWSAdapter::OnError(const std::string& error) {
  PE_LOG_ERROR("Replay error: {}", error);
  nlohmann::json msg;
  msg["type"] = "error";
  msg["message"] = error;
  std::lock_guard<std::mutex> lock(streams_mutex_);
  for (auto& [token, config] : active_streams_) { /* send error */
  }
}

bool ReplayWSAdapter::IsStreaming(const std::string& client_token) const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(streams_mutex_));
  return active_streams_.count(client_token) > 0;
}

std::string replay_ws::ReplayRequest::Serialize() const {
  nlohmann::json j;
  j["type"] = static_cast<uint8_t>(type);
  j["payload"] = payload;
  return j.dump();
}

std::optional<replay_ws::ReplayRequest> replay_ws::ReplayRequest::Deserialize(
    const std::string& json_str) {
  try {
    auto j = nlohmann::json::parse(json_str);
    ReplayRequest req;
    req.type = static_cast<ReplayMessageType>(j.value("type", 0));
    req.payload = j.value("payload", "");
    return req;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace poker_engine::replay
