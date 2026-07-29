#pragma once

#include "hyperparam.h"
#ifdef HAVE_EIGEN3
#include "bayesian_optimizer.h"
#include "sobol_analysis.h"
#endif
#include <functional>
#include <string>
#include <vector>

namespace poker_engine::cfr {

// ==================== 调优引擎 ====================

struct HyperParamTunerConfig {
  int bayesian_trials = 30;
  int sobol_samples = 500;
  int cfr_iterations_per_trial = 500;
  bool use_parallel_eval = true;
  double convergence_target = 0.01;
  std::string output_dir = "./tuning_results";
};

#ifndef HAVE_EIGEN3
struct SensitivityResult {
  int param_index = 0;
  double sensitivity = 0.0;
  std::string param_name;
};
#endif

class HyperParamTuner {
 public:
  using TunerConfig = HyperParamTunerConfig;

  explicit HyperParamTuner(const TunerConfig& config = TunerConfig());
  ~HyperParamTuner();

  // ========== 主流程 ==========

  // Phase 1: 敏感性分析 (确定重要参数)
  void RunSensitivityAnalysis();

  // Phase 2: 贝叶斯优化 (搜索最优配置)
  void RunBayesianOptimization();

  // Phase 3: 精细调优 (在最优解附近局部搜索)
  CFRHyperParams FineTune(const CFRHyperParams& baseline, double search_radius = 0.1);

  // ========== 结果查询 ==========

  CFRHyperParams BestConfig() const;
  double BestExploitability() const;

  // 调优报告
  std::string GenerateReport() const;

  // 保存/加载调优进度
  bool SaveProgress(const std::string& path) const;
  bool LoadProgress(const std::string& path);

 private:
  TunerConfig config_;

  HyperParamSpace param_space_;
#ifdef HAVE_EIGEN3
  BayesianOptimizer* bayesian_opt_ = nullptr;
  SobolAnalysis* sobal_ = nullptr;
#endif

  // 结果
  std::vector<TrainingResult> trial_results_;
  std::vector<SensitivityResult> sensitivity_results_;
  CFRHyperParams best_config_;
  double best_exploitability_ = std::numeric_limits<double>::max();

  // 内部方法
  TrainingResult EvaluateConfig(const CFRHyperParams& config);
  SensitivityResult ComputeSensitivity(int param_index);
};

}  // namespace poker_engine::cfr
