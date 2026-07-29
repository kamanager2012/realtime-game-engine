#pragma once
#include <atomic>
#include <functional>
#include <thread>
#include <unordered_set>

#include "message_router.h"
#include "ws_session.h"

// 前置声明 httplib
namespace httplib {
class Server;
class WebSocketConnection;
class Request;
class Response;
}  // namespace httplib

namespace poker_engine::network {

using WSHandler = std::function<void(WSSession::Ptr, const nlohmann::json&)>;
using CloseHandler = std::function<void(int64_t session_id, int64_t player_id)>;

struct WSServerConfig {
  std::string bind_address = "0.0.0.0";
  int port = 8443;
  int thread_count = 4;
  std::string ws_path = "/ws";
  int ping_interval_sec = 30;
};

// ==================== WebSocket 服务器 ====================
// 基于 cpp-httplib 的真实 I/O 服务器
// 职责: WS 握手、连接管理、消息分发、心跳

class WSServer {
 public:
  explicit WSServer(WSServerConfig config = {});
  ~WSServer();

  // 不可拷贝
  WSServer(const WSServer&) = delete;
  WSServer& operator=(const WSServer&) = delete;

  // ========== 生命周期 ==========
  bool Start();
  void Stop();

  // ========== 路由注册 ==========
  // 注册特定 op 的处理函数
  void RegisterHandler(int op, WSHandler handler);

  // 设置连接关闭回调
  void SetCloseHandler(CloseHandler handler);

  // ========== 广播 / 单播 ==========
  // 向单个 session 发送 JSON
  bool SendTo(int64_t session_id, const nlohmann::json& msg);

  // 向某个桌子的所有玩家广播
  void BroadcastToTable(int64_t table_id, const nlohmann::json& msg);

  // ========== 查询 ==========
  size_t ConnectionCount() const;
  WSSession::Ptr GetSession(int64_t session_id) const;

 private:
  // 内部：消息循环（每个连接一个线程）
  void MessageLoop(WSSession::Ptr session);

  // 内部：心跳线程
  void HeartbeatLoop();

  // 创建 session
  WSSession::Ptr CreateSession(int64_t player_id, const std::string& token);

  // 通过 native handle 查找 session
  WSSession::Ptr FindSessionByHandle(void* handle) const;

  static int64_t next_session_id_;

  WSServerConfig config_;
  std::unique_ptr<httplib::Server> server_;
  std::thread server_thread_;
  std::thread heartbeat_thread_;
  std::atomic<bool> running_{false};

  // session_id -> session
  mutable std::mutex sessions_mutex_;
  std::unordered_map<int64_t, WSSession::Ptr> sessions_;

  // native handle -> session (用于从 WS 回调中找到 session)
  mutable std::mutex handle_mutex_;
  std::unordered_map<void*, WSSession::Ptr> handle_map_;

  // table_id -> set of session_id
  mutable std::mutex table_mutex_;
  std::unordered_map<int64_t, std::unordered_set<int64_t>> table_sessions_;

  // op -> handler
  std::unordered_map<int, WSHandler> handlers_;
  CloseHandler close_handler_;
};

}  // namespace poker_engine::network
// DEPRECATED: Use phase13 WebSocketServer instead. This header retained for legacy spectator module.
