#pragma once

#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "poker_engine/cfr/types.h"

namespace poker_engine::cfr {

// ==================== 超参数定义 ====================

struct HyperParam {
  std::string name;
  double min_val;
  double max_val;
  double default_val;
  double step;     // 离散化步长 (0 = 连续)
  bool log_scale;  // 是否对数尺度
  std::string description;

  // 随机采样一个值
  double RandomSample(std::mt19937& rng) const;

  // 网格采样
  std::vector<double> GridSample() const;

  // 最近合法值
  double Snap(double value) const;
};

// ==================== CFR 超参数空间 ====================

struct CFRHyperParams {
  // ===== 核心训练参数 =====
  int num_iterations;        // 100 - 10,000,000
  double discount_alpha;     // 1.0 - 3.0 (遗憾折扣指数)
  double discount_beta;      // 0.3 - 1.0 (平均策略折扣指数)
  double exploit_threshold;  // 0.0001 - 1.0 (收敛阈值)

  // ===== 剪枝参数 =====
  bool enable_pruning;            // 是否启用剪枝
  double prune_regret_threshold;  // -1000 到 0 (低于此值剪枝)
  double prune_prob_threshold;    // 0 - 0.1 (剪枝概率阈值)

  // ===== 采样参数 =====
  int sampling_scheme;          // 0=全部, 1=单玩家, 2=外部采样
  double chance_sampling_prob;  // 0.1 - 1.0 (机会节点采样率)
  int rollout_depth;            // 0-3 (rollout 深度)

  // ===== 探索参数 =====
  double epsilon_greedy;     // 0 - 0.1 (ε-贪婪探索)
  double dirichlet_alpha;    // 0 - 10 (Dirichlet 噪声参数)
  double exploration_bonus;  // 0 - 100 (UCB 探索奖励)

  // ===== 策略改进 =====
  bool use_rm_plus;       // RM+ (负遗憾归零)
  bool use_discount;      // 是否使用折扣
  int discount_interval;  // 10 - 10000 迭代

  // ===== 树深度 =====
  int max_tree_depth;  // 1 - 20

  // ===== 并行参数 =====
  int num_threads;
  int batch_size;

  // 转换为向量 (用于优化器)
  std::vector<double> ToVector() const;
  void FromVector(const std::vector<double>& vec);

  // 参数名称列表
  static std::vector<std::string> ParamNames();

  // 参数数量
  static constexpr int kParamCount = 19;

  void Print() const;
};

// ==================== 搜索空间 ====================

class HyperParamSpace {
 public:
  HyperParamSpace();

  // 获取所有超参数定义
  const std::vector<HyperParam>& Params() const { return params_; }

  // 采样随机配置
  CFRHyperParams SampleRandom(std::mt19937& rng) const;

  // 从向量创建配置
  CFRHyperParams FromVector(const std::vector<double>& vec) const;

  // 配置 → 向量 (归一化到 [0,1])
  std::vector<double> ToNormalized(const CFRHyperParams& config) const;

  // 向量 → 配置
  CFRHyperParams FromNormalized(const std::vector<double>& norm) const;

  // 获取参数边界
  std::pair<std::vector<double>, std::vector<double>> Bounds() const;

  size_t Size() const { return params_.size(); }

 private:
  std::vector<HyperParam> params_;
  std::vector<size_t> int_params_;   // 整数参数索引
  std::vector<size_t> log_params_;   // 对数尺度参数索引
  std::vector<size_t> bool_params_;  // 布尔参数索引
};

// ==================== 训练结果 ====================

struct TrainingResult {
  CFRHyperParams config;
  double exploitability;    // 最终可剥削性
  double exploit_std;       // 标准差
  double time_seconds;      // 训练时间 (秒)
  size_t node_count;        // 信息集节点数
  double samples_per_sec;   // 采样速率
  int iterations_run;       // 实际迭代次数
  double convergence_rate;  // 收敛速度 (exploit 下降斜率)
  double quality_score;     // 综合评分 (越低越好)

  // Pareto 指标
  double Cost() const { return time_seconds * node_count / 1e6; }
  // 质量 = exploitability (越低越好)
};

}  // namespace poker_engine::cfr
