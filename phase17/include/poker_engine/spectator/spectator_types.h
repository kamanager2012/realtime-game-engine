#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "poker_engine/game/action.h"
#include "poker_engine/game/game_state.h"

namespace poker_engine::spectator {

// ==================== 观战消息协议 ====================

enum class SpectatorMessageType : uint8_t {
  // 服务端 → 客户端
  TournamentState = 0,    // 锦标赛完整状态
  HandState = 1,          // 单手牌状态更新
  HandEvent = 2,          // 单个事件（行动、亮牌等）
  LeaderboardUpdate = 3,  // 排行榜更新
  ChatMessage = 4,        // 聊天消息
  TournamentResult = 5,   // 锦标赛结果
  PlayerJoin = 6,         // 玩家加入观战
  PlayerLeave = 7,        // 玩家离开观战
  Heartbeat = 8,

  // 客户端 → 服务端
  Subscribe = 10,  // 订阅锦标赛
  Unsubscribe = 11,
  Chat = 12,
  RequestHistory = 13,  // 请求手牌历史
};

// ==================== 观战事件 ====================

struct SpectatorEvent {
  uint64_t sequence_id;
  uint64_t tournament_id;
  uint64_t hand_id;
  SpectatorMessageType type;
  double timestamp_ms;
  std::string payload;  // JSON

  std::string Serialize() const;
  static std::optional<SpectatorEvent> Deserialize(const std::string& json);
};

// ==================== 锦标赛状态快照 ====================

struct TournamentSnapshot {
  uint64_t tournament_id;
  std::string tournament_name;
  int status;  // TournamentStatus
  int current_blind_level;
  int64_t small_blind;
  int64_t big_blind;
  int64_t ante;
  int remaining_players;
  int total_players;
  int64_t prize_pool;
  double elapsed_seconds;

  // 当前手牌（如果有）
  struct CurrentHand {
    uint64_t hand_id;
    int phase;  // GamePhase
    int64_t pot;
    double current_bet;
    int dealer_seat;
    int acting_player_id;
    std::vector<uint8_t> community_cards;
    std::vector<std::string> action_history;  // JSON array
  };
  std::optional<CurrentHand> current_hand;

  // 玩家列表
  struct SpectatorPlayer {
    int64_t player_id;
    std::string display_name;
    double chips;
    int seat_index;
    int status;  // PlayerStatus
    double total_won;
    bool is_ai;
    bool is_active;
    int place;  // 0 = 未出局, 1+ = 名次
  };
  std::vector<SpectatorPlayer> players;

  // 排行榜（前10）
  std::vector<SpectatorPlayer> leaderboard;

  // 盲注计划预览
  std::vector<std::pair<int, std::string>> blind_schedule_preview;
};

// ==================== 观战配置 ====================

struct SpectatorConfig {
  uint16_t spectator_port = 9002;
  int max_spectators_per_tournament = 1000;
  int state_broadcast_interval_ms = 100;  // 状态广播间隔
  int chat_history_size = 200;
  bool enable_video_mode = false;  // 视频流模式（低延迟）
  double max_latency_ms = 500.0;   // 最大允许延迟
};

// ==================== 聊天消息 ====================

struct SpectatorChatMessage {
  int64_t player_id;
  std::string display_name;
  std::string message;
  std::chrono::system_clock::time_point timestamp;
  bool is_spectator;  // true = 观众, false = 玩家
};

}  // namespace poker_engine::spectator
