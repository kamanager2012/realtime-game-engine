#pragma once
#include <memory>

#include "message_router.h"
#include "poker_engine/game/action.h"
#include "poker_engine/game/table.h"
#include "ws_session.h"

namespace poker_engine::network {

class WSServer;

// ==================== Table 桥接器 ====================
// 连接网络层(WSServer)和游戏逻辑层(game::Table)
//
// game::Table 持有游戏状态但不感知网络
// TableBridge 是中间的胶水：
//   WS消息 → 解析 → 调用 game::Table 方法
//   game::Table 事件回调 → 构建JSON → 通过WS发送

class TableBridge {
 public:
  TableBridge(int64_t table_id, std::unique_ptr<poker_engine::game::Table> table,
              WSServer* ws_server);

  // ========== 网络 → 游戏逻辑 ==========
  void HandleAuth(WSSession::Ptr session, const nlohmann::json& payload);
  void HandleJoinTable(WSSession::Ptr session, const nlohmann::json& payload);
  void HandleLeaveTable(WSSession::Ptr session);
  void HandlePlayerAction(WSSession::Ptr session, const nlohmann::json& payload);
  void HandleChat(WSSession::Ptr session, const nlohmann::json& payload);
  void HandlePing(WSSession::Ptr session);
  void HandleStartHand();

  // ========== Bot ==========
  int AddBots(int count, int64_t buy_in);
  void ProcessBotActions();

  // ========== 属性 ==========
  int64_t table_id() const { return table_id_; }
  int64_t min_buy_in() const;
  const poker_engine::game::Table& table() const { return *table_; }
  poker_engine::game::Table& table_mut() { return *table_; }

 private:
  // 序列化完整桌子状态为 JSON（对指定 viewer 隐藏他人底牌）
  nlohmann::json BuildTableState(int64_t viewer_player_id = 0) const;

  // 发送完整状态给单个玩家
  void SendFullState(WSSession::Ptr session);

  // 发送/广播辅助
  void SendTo(WSSession::Ptr session, const nlohmann::json& msg);
  void SendTo(int64_t session_id, const nlohmann::json& msg);
  void Broadcast(const nlohmann::json& msg);
  void BroadcastExcept(int64_t exclude_session_id, const nlohmann::json& msg);

  int64_t FindSessionForPlayer(int64_t player_id) const;
  void SendError(WSSession::Ptr session, int code, const std::string& message);

  // Table 事件回调
  void OnTableEvent(const poker_engine::game::TableEvent& event);

  int64_t table_id_;
  std::unique_ptr<poker_engine::game::Table> table_;
  WSServer* ws_server_;  // 不拥有

  // player_id → session_id
  mutable std::mutex player_session_mutex_;
  std::unordered_map<int64_t, int64_t> player_session_map_;

  // session_id → player_id
  mutable std::mutex session_player_mutex_;
  std::unordered_map<int64_t, int64_t> session_player_map_;

  // Bot player IDs (negative)
  int next_bot_id_ = -1;
};

// ==================== 简易认证服务 ====================

class SimpleAuthService {
 public:
  void AddToken(const std::string& token, int64_t player_id);
  std::optional<int64_t> VerifyToken(const std::string& token) const;
  void RemoveToken(const std::string& token);

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, int64_t> token_map_;
};

}  // namespace poker_engine::network
