#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "poker_engine/anticheat/anticheat.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/security/behavior_analyzer.h"
#include "poker_engine/security/ip_reputation.h"

namespace poker_engine::security {

// ==================== 安全事件 ====================

struct SecurityEvent {
  uint64_t event_id;
  std::string player_id;
  std::string event_type;  // "connection", "action", "auth", "cheat"
  double risk_score;
  std::vector<std::string> indicators;
  std::string raw_data;  // 原始数据
  std::chrono::system_clock::time_point timestamp;
  bool auto_blocked = false;

  std::string ToString() const {
    return "SecurityEvent[" + std::to_string(event_id) + "]: player=" + player_id +
           " risk=" + std::to_string(risk_score) + " type=" + event_type;
  }
};

// ==================== 风险评估 ====================

struct RiskAssessment {
  std::string player_id;
  double overall_score;     // 0-100
  double ip_score;          // 来自 IP 信誉
  double device_score;      // 来自设备指纹分析
  double behavior_score;    // 来自行为分析
  double ml_score;          // 来自 ML 反作弊
  double historical_score;  // 来自历史数据

  std::vector<std::string> warnings;
  std::vector<std::string> critical_flags;
  double confidence;  // 0-1, 评估置信度

  enum class RiskLevel { Safe, Suspicious, HighRisk, Critical };

  RiskLevel GetLevel() const {
    if (overall_score >= 80) return RiskLevel::Critical;
    if (overall_score >= 60) return RiskLevel::HighRisk;
    if (overall_score >= 30) return RiskLevel::Suspicious;
    return RiskLevel::Safe;
  }

  std::string LevelToString() const {
    switch (GetLevel()) {
      case RiskLevel::Safe:
        return "SAFE";
      case RiskLevel::Suspicious:
        return "SUSPICIOUS";
      case RiskLevel::HighRisk:
        return "HIGH_RISK";
      case RiskLevel::Critical:
        return "CRITICAL";
    }
    return "UNKNOWN";
  }

  void Normalize() {
    // 加权平均
    overall_score = (ip_score * 0.15 + device_score * 0.10 + behavior_score * 0.35 +
                     ml_score * 0.30 + historical_score * 0.10);
    overall_score = std::max(0.0, std::min(100.0, overall_score));

    // 置信度基于可用数据的数量
    int data_points = 0;
    if (ip_score >= 0) data_points++;
    if (device_score >= 0) data_points++;
    if (behavior_score >= 0) data_points++;
    if (ml_score >= 0) data_points++;
    if (historical_score >= 0) data_points++;

    confidence = static_cast<double>(data_points) / 5.0;
  }
};

// ==================== 安全策略引擎 ====================

class SecurityPolicyEngine {
 public:
  struct Config {
    // 自动封锁阈值
    double auto_block_threshold = 90.0;
    double auto_kick_threshold = 75.0;
    double auto_flag_threshold = 40.0;

    // 行为分析权重
    double behavior_weight = 0.35;
    double ml_weight = 0.30;
    double ip_weight = 0.15;
    double device_weight = 0.10;
    double historical_weight = 0.10;

    // 冷却时间（防止重复封禁）
    int ban_cooldown_seconds = 3600;

    // 是否启用实时分析
    bool enable_realtime_analysis = true;

    // 学习率（行为分析的衰减因子）
    double learning_rate = 0.1;
  };

  explicit SecurityPolicyEngine(const Config& config);
  SecurityPolicyEngine();  // default config
  ~SecurityPolicyEngine();

  // ========== 初始化 ==========

  void Initialize();

  // ========== 实时评估 ==========

  // 评估连接
  RiskAssessment EvaluateConnection(const std::string& ip_address, const DeviceFingerprint& device,
                                    const anticheat::PlayerStatistics* existing_stats = nullptr);

  // 评估游戏行为（每次行动后调用）
  void EvaluateAction(int64_t player_id, const game::GameAction& action,
                      const game::GameState& state);

  // 批量评估（定期运行）
  std::vector<RiskAssessment> EvaluateAllPlayers();

  // ========== 决策 ==========

  enum class ActionType { Allow = 0, Monitor = 1, Flag = 2, Kick = 3, Ban = 4 };

  struct Decision {
    ActionType action;
    double confidence;
    std::string reason;
    std::vector<std::string> evidence;
  };

  Decision MakeDecision(const RiskAssessment& assessment);

  // ========== 状态更新 ==========

  // IP 信誉更新
  void ReportIPBehavior(const std::string& ip, ThreatLevel level, const std::string& reason);

  // 反作弊结果整合
  void IntegrateAntiCheatResult(const anticheat::CheatAlert& alert);

  // ========== 查询 ==========

  RiskAssessment GetPlayerRisk(int64_t player_id) const;
  bool IsPlayerFlagged(int64_t player_id) const;
  std::vector<uint64_t> GetActiveIncidents() const;

  // 统计
  struct Stats {
    int total_players_evaluated;
    int auto_blocks;
    int manual_reviews;
    float avg_risk_score;
    int active_incidents;
  };
  Stats GetStats() const;

  // 导出
  std::string ExportSecurityReport() const;

 private:
  Config config_;

  // 组件
  IPReputationManager ip_reputation_;
  anticheat::AntiCheatManager anticheat_manager_;
  std::unique_ptr<BehavioralAnalysisEngine> behavior_engine_;

  // 玩家风险评估
  mutable std::shared_mutex assessments_mutex_;
  std::unordered_map<int64_t, RiskAssessment> player_assessments_;

  // 事件日志
  std::vector<SecurityEvent> event_log_;
  mutable std::shared_mutex event_mutex_;

  // 统计
  Stats stats_ = {};

  // 封禁冷却
  std::unordered_map<int64_t, std::chrono::system_clock::time_point> ban_cooldown_;

  // 辅助
  void UpdateAssessment(int64_t player_id);
  bool IsInCooldown(int64_t player_id) const;
  void LogEvent(const SecurityEvent& event);
  std::string GenerateIncidentReport(const RiskAssessment& assessment) const;
};

}  // namespace poker_engine::security
