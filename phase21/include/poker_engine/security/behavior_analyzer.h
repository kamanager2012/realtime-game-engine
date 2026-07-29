#pragma once

#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "poker_engine/base/result.h"

namespace poker_engine::security {

// ==================== 玩家行为剖面 ====================

struct BehaviorSample {
  int64_t timestamp_ms;
  double action_latency_ms;  // 决策时间
  int action_type;           // 0=fold, 1=call, 2=bet, 3=raise, 4=all_in
  double bet_ratio;          // 下注额 / 底池
  double pot_ratio;          // 手牌风险比
  bool is_check;             // 是否过牌
  bool is_raise;             // 是否加注
  double stack_at_action;    // 行动时筹码量
  double avg_pot_last_5;     // 最近5手平均底池
};

struct BehavioralProfile {
  int64_t player_id;

  // 时间模式
  double avg_response_time_ms;
  double response_time_variance;
  double response_time_skewness;  // 偏度（检测自动化）
  double response_time_kurtosis;  // 峰度

  // 行为模式
  double vpip;           // 自愿投入底池率
  double pfr;            // 翻前加注率
  double agg_factor;     // 侵略因子
  double three_bet_pct;  // 3-bet 率
  double fold_to_3bet;   // 面对 3-bet 弃牌率
  double cbet_pct;       // 持续下注率
  double wtsd_pct;       // 摊牌率
  double wmsd_pct;       // 翻牌后赢钱率

  // 不一致性指标
  double strategy_volatility;   // 策略波动性
  double position_awareness;    // 位置意识评分
  double odd_action_frequency;  // 非标准操作频率

  // 关联分析
  std::vector<int64_t> common_opponents;
  double same_table_frequency;

  // 风险评分
  double risk_score;  // 0-100
  std::vector<std::string> risk_flags;

  void ComputeRiskScore();
  bool IsSuspicious() const { return risk_score >= 40.0; }
};

// ==================== 行为分析引擎 ====================

struct BehavioralAnalysisConfig {
  int min_samples_for_analysis = 30;
  double volatility_threshold = 0.3;      // 行为波动阈值
  double consistency_threshold = 0.15;    // 响应时间一致性阈值
  double optimal_play_threshold = 0.90;   // 最优游戏频率
  double position_diff_threshold = 0.10;  // 位置差异阈值
  int analyze_every_n_actions = 10;
};

class BehavioralAnalysisEngine {
 public:
  using Config = BehavioralAnalysisConfig;

  explicit BehavioralAnalysisEngine(const Config& config = Config());

  // 添加行为样本
  void AddSample(int64_t player_id, const BehaviorSample& sample);

  // 获取玩家的行为剖面
  std::optional<BehavioralProfile> GetProfile(int64_t player_id) const;

  // 比较两个玩家的行为相似度
  double CompareBehavior(int64_t player_a, int64_t player_b) const;

  // 检测时间相关（Time Correlation）
  // 两个玩家的行动时间模式是否高度相关
  double TimeCorrelation(int64_t player_a, int64_t player_b) const;

  // 检测策略镜像（Strategy Mirroring）
  // 两个玩家是否总是使用相同的策略
  double StrategyMirroring(int64_t player_a, int64_t player_b) const;

  // 分析所有玩家的行为
  std::vector<BehavioralProfile> AnalyzeAll();

  // 获取分析的玩家数量
  size_t PlayerCount() const {
    std::shared_lock lock(samples_mutex_);
    return behavior_samples_.size();
  }

 private:
  Config config_;

  // 每个玩家的行为样本
  std::unordered_map<int64_t, std::deque<BehaviorSample>> behavior_samples_;
  mutable std::shared_mutex samples_mutex_;

  // 内部分析方法
  BehavioralProfile ComputeProfile(int64_t player_id,
                                   const std::deque<BehaviorSample>& samples) const;
  double ComputeVolatility(const std::deque<BehaviorSample>& samples) const;
  double ComputeConsistency(const std::deque<BehaviorSample>& samples) const;
  double ComputePositionAwareness(const std::deque<BehaviorSample>& samples) const;
  double ComputeOptimalPlayRatio(const std::deque<BehaviorSample>& samples) const;
};

}  // namespace poker_engine::security
