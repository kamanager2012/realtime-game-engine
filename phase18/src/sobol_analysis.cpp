#include "poker_engine/cfr/sobol_analysis.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

#include "poker_engine/base/logging.h"

namespace poker_engine::cfr {

// ==================== SobolAnalysis ====================

SobolAnalysis::SobolAnalysis(int num_params)
    : num_params_(num_params),
      S1_(num_params, 0.0),
      ST_(num_params, 0.0),
      S1_conf_(num_params, 0.0),
      ST_conf_(num_params, 0.0) {
  InitDirectionNumbers(num_params);
}

void SobolAnalysis::InitDirectionNumbers(int dim) {
  // 简化的 Sobol 方向数生成
  // 实际项目应使用预计算的 Sobol 方向数 (e.g., from Joe & Kuo)
  direction_numbers_.resize(dim * 32, 1);

  // 使用简单的 LFSR 生成伪 Sobol 序列
  for (int d = 0; d < dim; ++d) {
    uint32_t v = 1u << (31 - d);
    for (int i = 0; i < 32; ++i) {
      direction_numbers_[d * 32 + i] = v;
      v ^= (v >> 1);
    }
  }
}

std::vector<double> SobolAnalysis::SobolSample(int dim) {
  std::vector<double> point(dim);
  uint32_t n = sobol_counter_++;
  uint32_t c = n;

  for (int d = 0; d < dim; ++d) {
    uint32_t v = 0;
    uint32_t mask = 1u;
    for (int i = 0; i < 32; ++i) {
      if (c & 1) v ^= direction_numbers_[d * 32 + i];
      c >>= 1;
      if (c == 0) break;
    }
    point[d] = v / static_cast<double>(1u << 32);
  }

  return point;
}

void SobolAnalysis::GenerateSamples(int N, const HyperParamSpace& space) {
  int D = num_params_;
  sobol_counter_ = 0;

  // Saltelli 扩展: N*(2D+2) 个样本
  int total_samples = N * (2 * D + 2);

  // 生成基础矩阵 A 和 B
  A_ = Eigen::MatrixXd(N, D);
  B_ = Eigen::MatrixXd(N, D);

  for (int i = 0; i < N; ++i) {
    auto sa = SobolSample(D);
    auto sb = SobolSample(D);
    for (int d = 0; d < D; ++d) {
      A_(i, d) = sa[d];
      B_(i, d) = sb[d];
    }
  }

  // 生成 AB 矩阵 (A 的第 j 列替换为 B 的第 j 列)
  AB_.resize(N * D, D);
  for (int j = 0; j < D; ++j) {
    for (int i = 0; i < N; ++i) {
      for (int d = 0; d < D; ++d) {
        AB_(j * N + i, d) = (d == j) ? B_(i, d) : A_(i, d);
      }
    }
  }

  PE_LOG_INFO("Sobol samples generated: {} total (N={}, D={})", total_samples, N, D);
}

void SobolAnalysis::ComputeIndices(const std::vector<double>& model_outputs) {
  int N = A_.rows();
  int D = num_params_;

  if (static_cast<int>(model_outputs.size()) != N * (2 * D + 2)) {
    PE_LOG_ERROR("Output size mismatch for Sobol analysis");
    return;
  }

  // 提取 f(A), f(B)
  std::vector<double> fA(N), fB(N);
  for (int i = 0; i < N; ++i) {
    fA[i] = model_outputs[i];
    fB[i] = model_outputs[N + i];
  }

  // 全局均值
  double f0 = 0.0;
  for (int i = 0; i < N; ++i) {
    f0 += fA[i] + fB[i];
  }
  f0 /= (2.0 * N);

  // 总方差
  double total_var = 0.0;
  for (int i = 0; i < N; ++i) {
    total_var += (fA[i] - f0) * (fA[i] - f0) + (fB[i] - f0) * (fB[i] - f0);
  }
  total_var /= (2.0 * N - 1);

  if (total_var < 1e-20) {
    PE_LOG_WARN("Sobol: zero variance in model output");
    std::fill(S1_.begin(), S1_.end(), 0.0);
    std::fill(ST_.begin(), ST_.end(), 0.0);
    return;
  }

  // 计算每个参数的 S1 和 ST
  for (int j = 0; j < D; ++j) {
    // fAB_j = model outputs for AB matrix with column j from B
    double s1_numerator = 0.0;
    double st_numerator = 0.0;

    for (int i = 0; i < N; ++i) {
      double fAB_j = model_outputs[N * (2 + j) + i];

      // S1: V[E(Y|x_j)] / V(Y)
      // 使用 Saltelli 公式: S1_j = (1/N) * sum(f(B) * (f(A_B^j) - f(A))) / V
      s1_numerator += fB[i] * (fAB_j - fA[i]);

      // ST: E[V(Y|x_~j)] / V(Y)  = 1 - V[E(Y|x_~j)] / V(Y)
      // ST_j = (1/N) * sum(f(A) * (f(A_B^j) - f(B))) / V
      st_numerator += fA[i] * (fAB_j - fB[i]);
    }

    S1_[j] = s1_numerator / (N * total_var);
    ST_[j] = st_numerator / (N * total_var);

    // 置信区间 (Bootstrap 近似)
    S1_conf_[j] = std::abs(S1_[j]) * 0.1;  // 简化
    ST_conf_[j] = std::abs(ST_[j]) * 0.1;
  }

  PE_LOG_INFO("Sobol analysis complete");
}

std::vector<int> SobolAnalysis::RankedParameters() const {
  std::vector<std::pair<int, double>> ranked;
  for (int i = 0; i < num_params_; ++i) {
    ranked.push_back({i, ST_[i]});
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  std::vector<int> result;
  for (auto& [idx, _] : ranked) result.push_back(idx);
  return result;
}

void SobolAnalysis::PrintResults() const {
  auto ranked = RankedParameters();

  std::cout << "\n";
  std::cout << "====================================================================" << std::endl;
  std::cout << "                    Sobol Sensitivity Analysis Results             " << std::endl;
  std::cout << "====================================================================" << std::endl;
  std::cout << std::left << std::setw(30) << "Parameter" << std::setw(12) << "S1 (Main)"
            << std::setw(12) << "ST (Total)" << std::setw(15) << "Importance" << std::endl;
  std::cout << "--------------------------------------------------------------------" << std::endl;

  for (int rank_idx : ranked) {
    double s1 = S1_[rank_idx];
    double st = ST_[rank_idx];
    std::string importance = SensitivityResult::ImportanceLabel(s1, st);

    std::cout << std::left << std::setw(30) << "param_" + std::to_string(rank_idx) << std::setw(12)
              << std::fixed << std::setprecision(4) << s1 << std::setw(12) << std::fixed
              << std::setprecision(4) << st << "  " << std::setw(12) << importance << std::endl;
  }

  std::cout << "====================================================================" << std::endl;
  std::cout << "\nTip: Prioritize tuning parameters with total-order index > 0.2" << std::endl;
}

}  // namespace poker_engine::cfr
