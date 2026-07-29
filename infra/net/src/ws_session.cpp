#include "poker_engine/network/ws_session.h"

#include <chrono>

namespace poker_engine::network {

void WSSession::Send(const nlohmann::json& j) { Send(j.dump()); }

void WSSession::Send(const std::string& raw) {
  std::lock_guard<std::mutex> lock(send_mutex_);

  if (!is_alive_) return;

  // 实际写入通过 WSServer 的原生 handle
  // 这里放入待发送队列，由 MessageLoop 消费
  pending_sends_.push(raw);
}

void WSSession::Send(int op, const std::string& data) {
  nlohmann::json msg;
  msg["op"] = op;
  if (!data.empty()) {
    try {
      msg["data"] = nlohmann::json::parse(data);
    } catch (...) {
      msg["data"] = data;
    }
  }
  Send(msg.dump());
}

}  // namespace poker_engine::network
