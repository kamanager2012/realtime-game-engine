#pragma once
#include <functional>
#include <unordered_map>

#include "ws_session.h"

namespace poker_engine::network {

// ==================== 消息路由 ====================
// 将 op 码路由到处理函数

struct Message {
  int64_t session_id;
  int64_t player_id;
  int op;
  nlohmann::json payload;
};

class MessageRouter {
 public:
  using Handler = std::function<void(const Message&)>;

  // 注册 op 处理函数
  void Register(int op, Handler handler);

  // 注销
  void Unregister(int op);

  // 路由消息
  // 返回: 是否找到了处理函数
  bool Route(const Message& msg) const;

  // 是否存在该 op 的处理
  bool HasHandler(int op) const;

 private:
  std::unordered_map<int, Handler> handlers_;
};

// ==================== 消息构建器 ====================
// 方便构建 JSON 消息

class MessageBuilder {
 public:
  explicit MessageBuilder(int op) { msg_["op"] = op; }

  MessageBuilder& Set(const std::string& key, const nlohmann::json& value) {
    msg_[key] = value;
    return *this;
  }

  MessageBuilder& Set(const std::string& key, int64_t value) {
    msg_[key] = value;
    return *this;
  }

  MessageBuilder& Set(const std::string& key, const std::string& value) {
    msg_[key] = value;
    return *this;
  }

  MessageBuilder& Set(const std::string& key, double value) {
    msg_[key] = value;
    return *this;
  }

  MessageBuilder& Set(const std::string& key, const std::vector<int64_t>& value) {
    msg_[key] = value;
    return *this;
  }

  nlohmann::json Build() const { return msg_; }
  std::string ToString() const { return msg_.dump(); }

 private:
  nlohmann::json msg_;
};

// ==================== 协议常量 ====================

namespace protocol {

// 客户端 → 服务器
inline constexpr int OP_AUTH = 1;            // 认证 {token}
inline constexpr int OP_JOIN_TABLE = 10;     // 加入桌子 {table_id}
inline constexpr int OP_LEAVE_TABLE = 11;    // 离开桌子
inline constexpr int OP_PLAYER_ACTION = 20;  // 玩家行动 {action, amount}
inline constexpr int OP_CHAT = 30;           // 聊天 {message}
inline constexpr int OP_PING = 100;          // 心跳

// 服务器 → 客户端
inline constexpr int OP_AUTH_RESULT = 101;     // {success, player_id}
inline constexpr int OP_TABLE_STATE = 201;     // 完整桌子状态
inline constexpr int OP_ACTION_REQUEST = 202;  // 请求玩家行动 {seat, pot, ...}
inline constexpr int OP_ACTION_RESULT = 203;   // 行动结果 {valid, error}
inline constexpr int OP_HAND_START = 210;      // 新手牌开始
inline constexpr int OP_HAND_END = 211;        // 手牌结束 {winners, payouts}
inline constexpr int OP_PLAYER_JOINED = 220;   // 有玩家加入
inline constexpr int OP_PLAYER_LEFT = 221;     // 有玩家离开
inline constexpr int OP_CHAT_MESSAGE = 301;    // 聊天消息 {player, message}
inline constexpr int OP_PONG = 400;            // 心跳响应
inline constexpr int OP_ERROR = 500;           // 错误 {code, message}

}  // namespace protocol

}  // namespace poker_engine::network
