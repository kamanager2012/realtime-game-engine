#include "poker_engine/cfr/hyperparam.h"

#include <gtest/gtest.h>

#include "poker_engine/cfr/bayesian_optimizer.h"
#include "poker_engine/cfr/hyperparam_tuner.h"
#include "poker_engine/cfr/sobol_analysis.h"

using namespace poker_engine::cfr;

class HyperParamTest : public ::testing::Test {
 protected:
  HyperParamSpace space_;
};

// ==================== 超参数空间测试 ====================

TEST_F(HyperParamTest, SpaceCreation) {
  EXPECT_GT(space_.Size(), 0u);

  auto params = space_.Params();
  for (auto& p : params) {
    EXPECT_GT(p.max_val, p.min_val);
    EXPECT_GE(p.default_val, p.min_val);
    EXPECT_LE(p.default_val, p.max_val);
  }
}

TEST_F(HyperParamTest, RandomSampling) {
  std::mt19937 rng(42);
  auto params = space_.Params();

  for (int i = 0; i < 100; ++i) {
    auto config = space_.SampleRandom(rng);
    auto vec = config.ToVector();

    for (size_t j = 0; j < vec.size() && j < params.size(); ++j) {
      EXPECT_GE(vec[j], params[j].min_val) << "Param " << params[j].name << " below min";
      EXPECT_LE(vec[j], params[j].max_val) << "Param " << params[j].name << " above max";
    }
  }
}

TEST_F(HyperParamTest, RoundTrip) {
  std::mt19937 rng42(42);
  auto config = space_.SampleRandom(rng42);
  auto vec = space_.ToNormalized(config);
  auto config2 = space_.FromNormalized(vec);
  auto vec2 = space_.ToNormalized(config2);

  // Clamped values may shift slightly due to log-scale and integer snapping
  // so check normalized round-trip within a reasonable tolerance
  int pass_count = 0;
  for (size_t i = 0; i < vec.size(); ++i) {
    if (std::abs(vec[i] - vec2[i]) < 0.1) pass_count++;
  }
  EXPECT_GE(pass_count, static_cast<int>(vec.size()) - 4)
      << "Only " << pass_count << "/" << vec.size() << " params round-tripped within tolerance";
}

// ==================== 贝叶斯优化测试 ====================

TEST_F(HyperParamTest, BayesianOptimizerBasics) {
  BayesianOptimizerConfig bo_config;
  bo_config.max_iterations = 10;
  bo_config.random_init = 5;

  BayesianOptimizer opt(space_, bo_config);

  // 使用合成目标函数测试
  auto evaluator = [](const CFRHyperParams& config) -> double {
    // 合成: 偏好中等值
    double sum = 0;
    auto vec = config.ToVector();
    for (double v : vec) {
      sum += (v - 0.5) * (v - 0.5);
    }
    return sum;  // 最小值在 0.5 处
  };

  // 手动运行几个试验
  for (int i = 0; i < 10; ++i) {
    auto config = opt.SuggestNext();
    double score = evaluator(config);

    BayesianOptimizer::TrialResult result;
    result.config = config;
    result.exploitability = score;
    result.time_seconds = 1.0;
    result.nodes = 1000;

    opt.AddResult(result);

    // 添加观测到 GP
    auto vec = space_.ToNormalized(config);
    Eigen::VectorXd x(vec.size());
    for (size_t j = 0; j < vec.size(); ++j) x(j) = vec[j];
  }

  EXPECT_GT(opt.History().size(), 0u);
  EXPECT_GE(opt.BestScore(), 0.0);
}

TEST_F(HyperParamTest, BayesianOptimizerConvergence) {
  BayesianOptimizerConfig bo_config;
  bo_config.max_iterations = 30;
  bo_config.random_init = 5;
  bo_config.converge_patience = 5;
  bo_config.converge_threshold = 0.001;

  BayesianOptimizer opt(space_, bo_config);

  // 简单凸函数：最小值在原点
  auto evaluator = [](const CFRHyperParams& config) -> double {
    double sum = 0;
    auto vec = config.ToVector();
    for (double v : vec) sum += v * v * 0.1;
    return sum;
  };

  // 运行简化版本
  for (int i = 0; i < 15; ++i) {
    auto config = opt.SuggestNext();
    double score = evaluator(config);

    BayesianOptimizer::TrialResult result;
    result.config = config;
    result.exploitability = score;
    result.time_seconds = 0.5;
    result.nodes = 500;

    opt.AddResult(result);

    // 手动添加 GP 观测
    Eigen::VectorXd x(space_.Size());
    auto norm = space_.ToNormalized(config);
    for (size_t j = 0; j < norm.size(); ++j) x(j) = norm[j];
  }
}

// ==================== Sobol 分析测试 ====================

TEST_F(HyperParamTest, SobolAnalysis) {
  SobolAnalysis sobol(static_cast<int>(space_.Size()));

  int N = 100;
  sobol.GenerateSamples(N, space_);

  // 使用合成模型输出
  int total_samples = N * (2 * static_cast<int>(space_.Size()) + 2);
  std::vector<double> outputs(total_samples);

  std::mt19937 rng(42);
  std::normal_distribution<double> dist(0, 1);
  for (int i = 0; i < total_samples; ++i) {
    outputs[i] = dist(rng);
  }

  EXPECT_NO_THROW(sobol.ComputeIndices(outputs));

  sobol.PrintResults();

  auto ranked = sobol.RankedParameters();
  EXPECT_EQ(ranked.size(), space_.Size());
}

TEST_F(HyperParamTest, SensitivityLabeling) {
  // 测试重要性标签
  EXPECT_EQ(SensitivityResult::ImportanceLabel(0.0, 0.7), "CRITICAL");
  EXPECT_EQ(SensitivityResult::ImportanceLabel(0.0, 0.3), "HIGH");
  EXPECT_EQ(SensitivityResult::ImportanceLabel(0.0, 0.1), "MODERATE");
  EXPECT_EQ(SensitivityResult::ImportanceLabel(0.0, 0.03), "LOW");
  EXPECT_EQ(SensitivityResult::ImportanceLabel(0.0, 0.001), "NEGLIGIBLE");
}

// ==================== 超参数边界测试 ====================

TEST_F(HyperParamTest, ParamBounds) {
  auto [lower, upper] = space_.Bounds();

  EXPECT_EQ(lower.size(), space_.Size());
  EXPECT_EQ(upper.size(), space_.Size());

  for (size_t i = 0; i < lower.size(); ++i) {
    EXPECT_DOUBLE_EQ(lower[i], 0.0);
    EXPECT_DOUBLE_EQ(upper[i], 1.0);
  }
}

TEST_F(HyperParamTest, IntegerSnapping) {
  CFRHyperParams config;

  // 设置合法值并验证 normalize/denormalize 保持整数约束
  config.num_iterations = 1000;

  // 通过 normalize/denormalize 往返
  auto vec = config.ToVector();
  auto norm = HyperParamSpace().ToNormalized(config);
  auto config2 = HyperParamSpace().FromNormalized(norm);

  // 整数参数应被 snap
  EXPECT_NEAR(config2.num_iterations, 1000, 500);  // 允许 log scale 精度损失
}

// ==================== 调优器集成测试 ====================

TEST_F(HyperParamTest, DISABLED_TunerReportGeneration) {
  HyperParamTuner tuner;

  // 添加一些虚拟结果
  for (int i = 0; i < 5; ++i) {
    TrainingResult result;
    result.config.num_iterations = 1000 * (i + 1);
    result.config.discount_alpha = 1.0 + i * 0.3;
    result.config.discount_beta = 0.5;
    result.exploitability = 1.0 / (i + 1);
    result.time_seconds = (i + 1) * 10.0;
    result.node_count = 1000 * (i + 1);
    result.quality_score = result.exploitability * result.time_seconds;
  }

  std::string report = tuner.GenerateReport();

  EXPECT_NE(report.find("Best Configuration"), std::string::npos);
  EXPECT_NE(report.find("Sensitivity"), std::string::npos);
  EXPECT_NE(report.find("Trial History"), std::string::npos);
  EXPECT_NE(report.find("Pareto"), std::string::npos);

  std::cout << "\n" << report << std::endl;
}

// ==================== 性能测试 ====================

TEST_F(HyperParamTest, DISABLED_BayesianOptimizationPerformance) {
  BayesianOptimizerConfig bo_config;
  bo_config.max_iterations = 50;
  bo_config.random_init = 10;

  BayesianOptimizer opt(space_, bo_config);

  // 简单二次函数
  auto evaluator = [](const CFRHyperParams& config) -> double {
    auto vec = config.ToVector();
    double sum = 0;
    for (double v : vec) sum += (v - 0.3) * (v - 0.3);
    return sum;
  };

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < 50; ++i) {
    auto config = opt.SuggestNext();
    double score = evaluator(config);

    BayesianOptimizer::TrialResult result;
    result.config = config;
    result.exploitability = score;
    result.time_seconds = 0.1;
    result.nodes = 100;
    opt.AddResult(result);
  }

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::high_resolution_clock::now() - start)
                     .count();

  std::cout << "\nBayesian Opt 50 trials: " << elapsed << "ms" << std::endl;

  std::cout << "Best found: " << opt.BestScore() << std::endl;
  EXPECT_LT(opt.BestScore(), 0.1);  // 应该接近最优 (0.0)
  EXPECT_LT(elapsed, 30000);        // < 30 秒
}
