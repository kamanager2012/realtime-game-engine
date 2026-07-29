#pragma once
#include <memory>
#include <mutex>
#include <unordered_map>

#include "auth_manager.h"
#include "table_bridge.h"
#include "ws_session.h"

namespace poker_engine::network {

// ==================== 连接管理器 ====================
// 管理"连接→认证→入桌"全生命周期

class ConnectionManager {
 public:
  ConnectionManager(WSServer* server, std::shared_ptr<TableBridge> bridge)
      : server_(server), bridge_(bridge) {}

  // WS 连接建立时调用
  void OnConnect(WSSession::Ptr session) {
    std::lock_guard<std::mutex> lock(mutex_);

    pending_sessions_[session->session_id()] = session;
  }

  // 收到认证消息时调用
  void OnAuthMessage(WSSession::Ptr session, const nlohmann::json& data) {
    std::string token = data.value("token", "");

    // 方式 1：简单 token（player_id 直转）
    auto auth_result = SimpleAuth(token);

    if (auth_result) {
      int64_t player_id = *auth_result;

      // 认证成功
      session->set_authenticated(true);
      auth_.UpdateSession(player_id, session->session_id());

      // 存入活跃连接
      {
        std::lock_guard<std::mutex> lock(mutex_);
        active_players_[player_id] = session;
        pending_sessions_.erase(session->session_id());
      }

      // 返回认证结果
      nlohmann::json resp;
      resp["op"] = protocol::OP_AUTH_RESULT;
      resp["data"]["success"] = true;
      resp["data"]["player_id"] = player_id;
      session->Send(resp);
    } else {
      // 认证失败
      nlohmann::json resp;
      resp["op"] = protocol::OP_AUTH_RESULT;
      resp["data"]["success"] = false;
      resp["data"]["error"] = "Invalid token";
      session->Send(resp);

      // 3 秒后断开
      session->Close();
    }
  }

  // 收到加入桌子请求
  void OnJoinTable(WSSession::Ptr session, const nlohmann::json& data) {
    int64_t player_id = session->player_id();
    int64_t table_id = data.value("table_id", -1LL);

    if (table_id != bridge_->table_id()) {
      bridge_->SendError(session, 404, "Table not found");
      return;
    }

    // 检查玩家余额（TODO: 查询数据库）
    int64_t chips = 10000;  // 测试用

    if (chips < bridge_->min_buy_in()) {
      bridge_->SendError(session, 400, "Insufficient chips");
      return;
    }

    // 加入桌子
    bridge_->HandleJoinTable(session, data);

    // 通知其他玩家
    nlohmann::json notify;
    notify["op"] = protocol::OP_PLAYER_JOINED;
    notify["data"]["player_id"] = player_id;
    bridge_->BroadcastExcept(session->session_id(), notify);
  }

  // 断开连接
  void OnDisconnect(int64_t session_id, int64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    active_players_.erase(player_id);
    pending_sessions_.erase(session_id);
  }

  // 查询玩家的 session
  WSSession::Ptr GetPlayerSession(int64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_players_.find(player_id);
    return it != active_players_.end() ? it->second : nullptr;
  }

  // 当前活跃连接数
  size_t ActiveCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_players_.size();
  }

 private:
  // 简单 token 验证（token = player_id 字符串）
  std::optional<int64_t> SimpleAuth(const std::string& token) {
    try {
      int64_t pid = std::stoll(token);
      if (pid > 0) return pid;
    } catch (...) {
    }
    return std::nullopt;
  }

  WSServer* server_;
  std::shared_ptr<TableBridge> bridge_;

  mutable std::mutex mutex_;

  // 已认证的玩家
  std::unordered_map<int64_t, WSSession::Ptr> active_players_;

  // 尚未认证的会话
  std::unordered_map<int64_t, WSSession::Ptr> pending_sessions_;

  AuthManager auth_;
};

}  // namespace poker_engine::network
