#pragma once

#include <algorithm>
#include <cmath>
#include <Eigen/Dense>
#include <limits>
#include <optional>
#include <random>
#include <vector>

#include "hyperparam.h"

namespace poker_engine::cfr {

// ==================== 高斯过程回归 ====================

class GaussianProcess {
 public:
  using Matrix = Eigen::MatrixXd;
  using Vector = Eigen::VectorXd;

  explicit GaussianProcess(double noise_variance = 1e-4, double length_scale = 1.0,
                           double signal_variance = 1.0);

  // 添加观测
  void AddObservation(const Vector& x, double y);

  // 预测均值和方差
  std::pair<double, double> Predict(const Vector& x) const;

  // 批量预测
  void PredictBatch(const Matrix& X, Vector& means, Vector& variances) const;

  // 获取观测数量
  size_t NumObservations() const { return X_obs_.rows(); }

  // 重置
  void Clear();

  // 超参数获取
  double length_scale() const { return length_scale_; }
  double signal_variance() const { return signal_variance_; }
  double noise_variance() const { return noise_variance_; }

 private:
  double length_scale_;
  double signal_variance_;
  double noise_variance_;

  Matrix X_obs_;
  Vector y_obs_;
  Matrix K_inv_;  // 协方差矩阵的逆
  bool needs_update_ = true;

  // 核函数 (RBF / Matern 2.5)
  double Kernel(const Vector& a, const Vector& b) const;
  Matrix ComputeKernelMatrix(const Matrix& X1, const Matrix& X2) const;

  // 更新逆矩阵
  void UpdateInverse();

  // 计算两点间距离矩阵
  Matrix PairwiseDistances(const Matrix& X1, const Matrix& X2) const;
};

// ==================== 采集函数 ====================

enum class AcquisitionFunction {
  ExpectedImprovement,       // 期望改进
  UpperConfidenceBound,      // UCB
  ProbabilityOfImprovement,  // 改进概率
};

class Acquisition {
 public:
  using Vector = Eigen::VectorXd;

  static Vector ExpectedImprovement(const GaussianProcess& gp, const Eigen::MatrixXd& candidates,
                                    double best_y, double exploration = 0.01);

  static Vector UpperConfidenceBound(const GaussianProcess& gp, const Eigen::MatrixXd& candidates,
                                     double kappa = 2.0);

  static Vector ProbabilityOfImprovement(const GaussianProcess& gp,
                                         const Eigen::MatrixXd& candidates, double best_y,
                                         double xi = 0.01);
};

// ==================== 贝叶斯优化器 ====================

struct BayesianOptimizerConfig {
  int max_iterations = 50;           // 总迭代次数
  int random_init = 10;              // 随机初始化次数
  double converge_threshold = 1e-4;  // 收敛阈值
  int converge_patience = 10;        // 收敛耐心
  AcquisitionFunction acq_func = AcquisitionFunction::ExpectedImprovement;
  double exploration = 0.01;  // 探索参数
  double kappa = 2.0;         // UCB 参数

  // 每次训练评估的最大迭代次数
  int train_iterations_per_trial = 1000;
  bool early_stop = true;
  double early_stop_threshold = 0.01;
};

class BayesianOptimizer {
 public:
  explicit BayesianOptimizer(const HyperParamSpace& space,
                             const BayesianOptimizerConfig& config = BayesianOptimizerConfig());

  // 执行一次试验
  struct TrialResult {
    CFRHyperParams config;
    double exploitability;
    double time_seconds;
    size_t nodes;

    double Cost() const { return time_seconds; }
    double quality_score = 0.0;  // 综合评分 (越低越好)
  };

  // 运行优化
  void Optimize(int total_trials);

  // 获取最佳配置
  CFRHyperParams BestConfig() const;
  double BestScore() const;

  // 获取历史
  const std::vector<TrialResult>& History() const { return history_; }

  // 建议下一个试验点
  CFRHyperParams SuggestNext();

  // 添加试验结果
  void AddResult(const TrialResult& result);

  // 收敛检测
  bool HasConverged() const;

  // Pareto 前沿
  std::vector<size_t> ParetoFront() const;

 private:
  const HyperParamSpace& space_;
  BayesianOptimizerConfig config_;

  GaussianProcess gp_;
  std::vector<TrialResult> history_;
  mutable std::mt19937 rng_;

  // 内部方法
  Eigen::VectorXd ParamsToVector(const CFRHyperParams& params) const;
  CFRHyperParams VectorToParams(const Eigen::VectorXd& vec) const;

  // 多起点优化采集函数
  Eigen::VectorXd OptimizeAcquisition(int num_restarts);

  // 获取归一化的历史 exploitability
  std::vector<double> GetNormalizedScores() const;
};

}  // namespace poker_engine::cfr
