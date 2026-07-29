#pragma once

#include <algorithm>
#include <cmath>
#include <Eigen/Dense>
#include <numeric>
#include <random>
#include <vector>

#include "hyperparam.h"

namespace poker_engine::cfr {

// ==================== Sobol 敏感性分析 ====================
// 使用 Saltelli 扩展的 Sobol 序列估算一阶和总阶 Sobol 指数

class SobolAnalysis {
 public:
  explicit SobolAnalysis(int num_params);

  // 生成 Sobol 序列样本
  // 参数: N = 基础样本数量, D = 参数数量
  // 总样本数 = N * (2D + 2)
  void GenerateSamples(int N, const HyperParamSpace& space);

  // 计算 Sobol 指数
  // model_outputs: 每个样本对应的模型输出 (exploitability)
  void ComputeIndices(const std::vector<double>& model_outputs);

  // 获取一阶指数 (每个参数的主效应)
  const std::vector<double>& FirstOrderIndices() const { return S1_; }

  // 获取总阶指数 (包括交互效应)
  const std::vector<double>& TotalOrderIndices() const { return ST_; }

  // 获取参数排名 (按总阶指数降序)
  std::vector<int> RankedParameters() const;

  // 打印分析结果
  void PrintResults() const;

 private:
  int num_params_;

  std::vector<double> S1_;       // 一阶指数
  std::vector<double> ST_;       // 总阶指数
  std::vector<double> S1_conf_;  // 置信区间
  std::vector<double> ST_conf_;

  Eigen::MatrixXd A_;   // 基础矩阵 A
  Eigen::MatrixXd B_;   // 基础矩阵 B
  Eigen::MatrixXd AB_;  // 混合矩阵

  // Sobol 序列生成
  std::vector<uint32_t> direction_numbers_;
  uint32_t sobol_counter_ = 0;

  void InitDirectionNumbers(int dim);
  std::vector<double> SobolSample(int dim);

  double Variance(const std::vector<double>& values, double mean);
};

// ==================== 敏感性分析结果 ====================

struct SensitivityResult {
  int param_index;
  std::string param_name;
  double first_order;           // 一阶指数
  double total_order;           // 总阶指数
  double confidence;            // 置信度
  std::string importance_rank;  // 定性描述

  static std::string ImportanceLabel(double s1, double st) {
    if (st > 0.5) return "CRITICAL";
    if (st > 0.2) return "HIGH";
    if (st > 0.05) return "MODERATE";
    if (st > 0.01) return "LOW";
    return "NEGLIGIBLE";
  }
};

}  // namespace poker_engine::cfr
