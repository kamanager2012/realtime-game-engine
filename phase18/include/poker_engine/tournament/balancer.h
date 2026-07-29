#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <vector>

#include "poker_engine/tournament/tournament.h"

namespace poker_engine::tournament {

// ==================== 瑞士轮配对算法 ====================

struct SwissPairing {
  int64_t player_a;
  int64_t player_b;
  int table_id;
  int seat_a;
  int seat_b;
  double match_quality;  // 0-1, 越高越公平

  // 颜色分配 (用于棋类，扑克不需要，但保留以便扩展)
  bool player_a_white;
};

enum class TiebreakMethod {
  Buchholz,         // 对手分总和
  Dorney,           // 改进的 Buchholz
  Cumulative,       // 累积分
  SonnebornBerger,  // SB 得分 (胜者分)
  MostWins,         // 胜场最多
};

struct SwissSystemConfig {
  TiebreakMethod tiebreak = TiebreakMethod::Buchholz;
  float ideal_pairing_score_diff = 0.0f;
  bool avoid_repeat_pairs = true;
  bool fold_in_top_group = false;
  int fold_in_threshold = 4;
};

class SwissSystem {
 public:
  using SwissConfig = SwissSystemConfig;

  explicit SwissSystem(const SwissConfig& config = SwissConfig()) : config_(config) {}

  // 生成配对
  std::vector<SwissPairing> GeneratePairings(
      const std::vector<TournamentPlayer>& players,
      const std::vector<std::pair<int64_t, int64_t>>& previous_rounds);

  // 计算破同分
  std::vector<std::pair<int64_t, float>> ComputeTiebreaks(
      const std::vector<TournamentPlayer>& players,
      const std::vector<std::vector<std::pair<int64_t, int64_t>>>& round_history);

 private:
  SwissConfig config_;

  // 按分数分组
  std::vector<std::vector<int64_t>> ScoreGroups(const std::vector<TournamentPlayer>& players);

  // 组内配对
  std::vector<SwissPairing> PairWithinGroup(
      const std::vector<int64_t>& group, const std::vector<TournamentPlayer>& players,
      const std::vector<std::pair<int64_t, int64_t>>& previous_pairings);

  // 计算配对质量
  double PairingQuality(int64_t id_a, int64_t id_b, const std::vector<TournamentPlayer>& players,
                        const TournamentPlayer& pa, const TournamentPlayer& pb);
};

// ==================== 座位分配器 ====================

class SeatAllocator {
 public:
  struct SeatPreference {
    int64_t player_id;
    int preferred_seat;  // -1 = 无偏好
    bool avoid_window;
    bool prefer_center;
  };

  // 最优座位分配 (匈牙利算法)
  static std::vector<int> OptimalAssignment(int num_seats,
                                            const std::vector<SeatPreference>& preferences);

  // 快速近似分配
  static std::vector<int> GreedyAssignment(int num_seats,
                                           const std::vector<SeatPreference>& preferences);

 private:
  // 匈牙利算法实现
  static std::vector<int> HungarianAlgorithm(const std::vector<std::vector<double>>& cost_matrix);
};

// ==================== 断线替补 ====================

struct SubstitutionRequest {
  int64_t original_player_id;    // 断线玩家
  int64_t substitute_player_id;  // 替补玩家 (来自等候名单)
  int64_t timestamp;
  bool approved;
};

class SubstitutionManager {
 public:
  // 请求替补
  bool RequestSubstitution(int64_t original_id, int64_t substitute_id);

  // 自动替补 (从等候名单选择最佳候选人)
  std::optional<int64_t> AutoSubstitute(
      int64_t original_id, const std::vector<int64_t>& waitlist,
      const std::unordered_map<int64_t, TournamentPlayer>& players);

  // 审批替补
  bool ApproveSubstitution(int64_t original_id, int64_t substitute_id);

  // 获取替补历史
  const std::vector<SubstitutionRequest>& History() const { return history_; }

 private:
  std::vector<SubstitutionRequest> history_;

  // 替补评分
  static double SubstituteScore(int64_t candidate_id, int64_t original_id,
                                const std::unordered_map<int64_t, TournamentPlayer>& players);
};

// ==================== 锦标赛动态调节器 ====================
// 根据实时数据分析动态调整盲注结构和 AI 数量

struct BalancingConfig {
  double min_players_for_reg_boost = 3;  // 低于此人数开启注册加速
  int max_ai_players = 4;                // 最多AI玩家
  int min_ai_players = 1;                // 最少AI玩家 (保持桌子活跃)
  double target_avg_pot = 50.0;          // 目标平均底池
  double blind_adjustment_rate = 0.1;    // 盲注调整速率
};

class TournamentBalancer {
 public:
  using BalancingConfig_ = BalancingConfig;

  TournamentBalancer(const BalancingConfig& config = BalancingConfig()) : config_(config) {}

  // 分析锦标赛状态并返回建议
  struct BalanceRecommendation {
    int suggested_blind_multiplier;  // 当前盲注倍数
    int suggested_ai_count;          // 建议 AI 数量
    bool should_open_registration;   // 是否开放新注册
    bool should_merge_tables;        // 是否合并桌子
    std::string reason;
  };

  BalanceRecommendation Analyze(const TournamentConfig& config,
                                const std::vector<TournamentPlayer>& active_players,
                                double avg_pot_recent, double avg_hand_time_seconds);

 private:
  BalancingConfig config_;
};

}  // namespace poker_engine::tournament
