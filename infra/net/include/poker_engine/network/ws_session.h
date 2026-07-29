#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <string>

namespace poker_engine::network {

// ==================== WebSocket 会话 ====================
// 代表一个客户端连接，从 WS 握手创建到断开销毁

class WSSession {
 public:
  using Ptr = std::shared_ptr<WSSession>;

  WSSession(int64_t session_id, int64_t player_id, const std::string& token)
      : session_id_(session_id),
        player_id_(player_id),
        auth_token_(token),
        authenticated_(false),
        created_at_(Now()),
        last_active_(created_at_),
        is_alive_(true) {}

  // === 属性 ===
  int64_t session_id() const { return session_id_; }
  int64_t player_id() const { return player_id_; }
  std::string token() const { return auth_token_; }
  bool authenticated() const { return authenticated_.load(); }
  bool is_alive() const { return is_alive_.load(); }
  uint64_t last_active() const { return last_active_.load(); }

  // 该 session 所在的桌子 -1 表示未入座
  int64_t table_id() const { return table_id_.load(); }
  void set_table_id(int64_t tid) { table_id_ = tid; }

  // === 发送消息 ===
  // 线程安全：加锁后写入发送队列
  void Send(const nlohmann::json& j);
  void Send(const std::string& raw);

  void Send(int op, const std::string& data = "");
  // op 定义:
  //   0 = 心跳响应
  //   1 = 错误
  //   2 = 桌子状态推送
  //   3 = 玩家行动请求
  //   4 = 聊天消息
  //   5 = 重连状态同步
  //  10 = 认证结果
  //  20 = 加入桌子结果
  //  30 = 行动结果
  //  40 = 局内状态快照

  // === 心跳 ===
  void Touch() { last_active_ = Now(); }
  bool IsTimeout(int timeout_sec = 300) const {
    return (Now() - last_active_.load()) / 1000 > static_cast<uint64_t>(timeout_sec);
  }

  // === 标记关闭 ===
  void Close() { is_alive_ = false; }

  // === 原始连接指针（由 WSServer 管理）===
  void* native_handle() const { return native_handle_; }
  void set_native_handle(void* h) { native_handle_ = h; }

 private:
  static uint64_t Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  int64_t session_id_;
  int64_t player_id_;
  std::string auth_token_;
  std::atomic<bool> authenticated_;

  uint64_t created_at_;
  std::atomic<uint64_t> last_active_;
  std::atomic<bool> is_alive_;
  std::atomic<int64_t> table_id_{-1};

  void* native_handle_ = nullptr;  // 底层 WS 连接指针

  mutable std::mutex send_mutex_;
  std::queue<std::string> pending_sends_;
};

}  // namespace poker_engine::network
