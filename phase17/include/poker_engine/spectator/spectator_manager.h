#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "poker_engine/tournament/tournament.h"
#include "poker_engine/tournament/tournament_server.h"
#include "spectator_types.h"

namespace poker_engine::spectator {

// ==================== 观战会话 ====================

struct SpectatorSession {
  std::string token;
  int64_t session_id;
  int64_t player_id;  // -1 = 纯观众
  std::string display_name;
  uint64_t subscribed_tournament_id;
  std::chrono::steady_clock::time_point connected_at;
  std::chrono::steady_clock::time_point last_heartbeat;
  bool is_video_mode = false;  // 请求低延迟模式
  std::queue<SpectatorEvent> pending_events;
  std::mutex event_mutex;

  bool IsAlive() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat).count();
    return elapsed < 30;  // 30秒无心跳断开
  }
};

// ==================== 事件广播器 ====================

class EventBroadcaster {
 public:
  EventBroadcaster();

  // 注册/注销观战会话
  void RegisterSession(std::shared_ptr<SpectatorSession> session);
  void UnregisterSession(int64_t session_id);

  // 广播事件到指定锦标赛的所有观众
  void Broadcast(uint64_t tournament_id, const SpectatorEvent& event);

  // 单播到特定会话
  void Unicast(int64_t session_id, const SpectatorEvent& event);

  // 获取锦标赛的观众数量
  int SpectatorCount(uint64_t tournament_id) const;

 private:
  struct TournamentViewers {
    std::unordered_map<int64_t, std::shared_ptr<SpectatorSession>> sessions;
    mutable std::shared_mutex mutex;
  };

  std::unordered_map<uint64_t, TournamentViewers> tournament_viewers_;
  mutable std::shared_mutex global_mutex_;
  int64_t next_session_id_ = 1;
};

// ==================== 观战管理器 ====================
// 连接锦标赛和 WebSocket 服务

class SpectatorManager {
 public:
  explicit SpectatorManager(const SpectatorConfig& config = SpectatorConfig());
  ~SpectatorManager();

  // 关联锦标赛服务器
  void SetTournamentServer(tournament::TournamentServer* ts) { tournament_server_ = ts; }

  // 启动观战服务
  bool Start();
  void Stop();

  // 观战者操作
  bool Subscribe(int64_t session_id, uint64_t tournament_id);
  bool Unsubscribe(int64_t session_id);
  void HandleChat(int64_t session_id, const std::string& message);

  // 获取锦标赛状态快照
  TournamentSnapshot GetSnapshot(uint64_t tournament_id) const;

  // 请求历史手牌
  std::vector<SpectatorEvent> GetHandHistory(uint64_t tournament_id, uint64_t hand_id,
                                             int limit = 50) const;

  // 锦标赛回调（由 TournamentServer 调用）
  void OnTournamentEvent(uint64_t tournament_id, const tournament::TournamentEvent& event);
  void OnHandEvent(uint64_t tournament_id, uint64_t hand_id, const game::GameAction& action);
  void OnHandComplete(uint64_t tournament_id, uint64_t hand_id, const game::GameState& final_state);

  // 事件广播入口（由 TournamentBroadcaster 调用）
  void BroadcastEvent(uint64_t tournament_id, const SpectatorEvent& event) {
    broadcaster_.Broadcast(tournament_id, event);
  }

  // 广播排行榜更新（不触发 HandState）
  void BroadcastLeaderboard(uint64_t tournament_id) {
    auto evt = CreateLeaderboardEvent(tournament_id);
    broadcaster_.Broadcast(tournament_id, evt);
  }

  // 创建锦标赛状态事件（供外部调用）
  SpectatorEvent CreateTournamentStateEvent(uint64_t tournament_id) const;

  // 统计
  struct Stats {
    int total_sessions = 0;
    int active_sessions = 0;
    int events_broadcast = 0;
    double avg_broadcast_latency_ms = 0;
  };
  Stats GetStats() const;

 private:
  void BroadcastLoop();
  void CleanupExpiredSessions();
  SpectatorEvent CreateLeaderboardEvent(uint64_t tournament_id) const;

  SpectatorConfig config_;
  tournament::TournamentServer* tournament_server_ = nullptr;

  EventBroadcaster broadcaster_;
  std::unordered_map<int64_t, std::shared_ptr<SpectatorSession>> sessions_;
  mutable std::shared_mutex sessions_mutex_;

  // 广播线程
  std::atomic<bool> running_{false};
  std::thread broadcast_thread_;

  // 事件队列
  struct PendingBroadcast {
    uint64_t tournament_id;
    SpectatorEvent event;
    std::chrono::steady_clock::time_point enqueue_time;
  };
  std::queue<PendingBroadcast> broadcast_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;

  // 统计
  std::atomic<int64_t> total_events_broadcast_{0};
  std::atomic<int64_t> total_broadcast_latency_us_{0};
  int cleanup_counter_ = 0;
};

}  // namespace poker_engine::spectator
