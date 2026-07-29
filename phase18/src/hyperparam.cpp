#include "poker_engine/cfr/hyperparam.h"

#include <algorithm>
#include <numeric>
#include <random>

#include "poker_engine/base/logging.h"

namespace poker_engine::cfr {

// ==================== HyperParam ====================

double HyperParam::RandomSample(std::mt19937& rng) const {
  if (log_scale) {
    std::uniform_real_distribution<double> dist(std::log10(min_val), std::log10(max_val));
    return std::pow(10.0, dist(rng));
  }
  std::uniform_real_distribution<double> dist(min_val, max_val);
  return dist(rng);
}

std::vector<double> HyperParam::GridSample() const {
  std::vector<double> values;
  if (step > 0) {
    for (double v = min_val; v <= max_val + step * 0.5; v += step) {
      values.push_back(std::min(v, max_val));
    }
  } else {
    // 连续: 默认采样 10 个点
    for (int i = 0; i <= 10; ++i) {
      double t = static_cast<double>(i) / 10.0;
      values.push_back(min_val + t * (max_val - min_val));
    }
  }
  return values;
}

double HyperParam::Snap(double value) const {
  if (step > 0) {
    double snapped = std::round(value / step) * step;
    return std::max(min_val, std::min(max_val, snapped));
  }
  return std::max(min_val, std::min(max_val, value));
}

// ==================== CFRHyperParams ====================

std::vector<double> CFRHyperParams::ToVector() const {
  std::vector<double> v = {
      static_cast<double>(num_iterations),
      discount_alpha,
      discount_beta,
      exploit_threshold,
      enable_pruning ? 1.0 : 0.0,
      prune_regret_threshold,
      prune_prob_threshold,
      static_cast<double>(sampling_scheme),
      chance_sampling_prob,
      static_cast<double>(rollout_depth),
      epsilon_greedy,
      dirichlet_alpha,
      exploration_bonus,
      use_rm_plus ? 1.0 : 0.0,
      use_discount ? 1.0 : 0.0,
      static_cast<double>(discount_interval),
      static_cast<double>(max_tree_depth),
      static_cast<double>(num_threads),
      static_cast<double>(batch_size),
  };
  return v;
}

void CFRHyperParams::FromVector(const std::vector<double>& vec) {
  if (vec.size() < kParamCount) return;
  size_t i = 0;
  num_iterations = static_cast<int>(vec[i++]);
  discount_alpha = vec[i++];
  discount_beta = vec[i++];
  exploit_threshold = vec[i++];
  enable_pruning = vec[i++] > 0.5;
  prune_regret_threshold = vec[i++];
  prune_prob_threshold = vec[i++];
  sampling_scheme = static_cast<int>(vec[i++]);
  chance_sampling_prob = vec[i++];
  rollout_depth = static_cast<int>(vec[i++]);
  epsilon_greedy = vec[i++];
  dirichlet_alpha = vec[i++];
  exploration_bonus = vec[i++];
  use_rm_plus = vec[i++] > 0.5;
  use_discount = vec[i++] > 0.5;
  discount_interval = static_cast<int>(vec[i++]);
  max_tree_depth = static_cast<int>(vec[i++]);
  num_threads = static_cast<int>(vec[i++]);
  batch_size = static_cast<int>(vec[i++]);
}

std::vector<std::string> CFRHyperParams::ParamNames() {
  return {"num_iterations",
          "discount_alpha",
          "discount_beta",
          "exploit_threshold",
          "enable_pruning",
          "prune_regret_threshold",
          "prune_prob_threshold",
          "sampling_scheme",
          "chance_sampling_prob",
          "rollout_depth",
          "epsilon_greedy",
          "dirichlet_alpha",
          "exploration_bonus",
          "use_rm_plus",
          "use_discount",
          "discount_interval",
          "max_tree_depth",
          "num_threads",
          "batch_size"};
}

void CFRHyperParams::Print() const {
  PE_LOG_INFO("CFR HyperParams:");
  PE_LOG_INFO("  iterations={}, alpha={:.2f}, beta={:.2f}, threshold={:.4f}", num_iterations,
              discount_alpha, discount_beta, exploit_threshold);
  PE_LOG_INFO("  pruning={}, prune_regret={:.1f}, prune_prob={:.3f}", enable_pruning,
              prune_regret_threshold, prune_prob_threshold);
  PE_LOG_INFO("  sampling={}, chance_prob={:.2f}, rollout_depth={}", sampling_scheme,
              chance_sampling_prob, rollout_depth);
  PE_LOG_INFO("  epsilon={:.3f}, dirichlet={:.2f}, exploration={:.1f}", epsilon_greedy,
              dirichlet_alpha, exploration_bonus);
  PE_LOG_INFO("  rm_plus={}, discount={}, interval={}, max_depth={}", use_rm_plus, use_discount,
              discount_interval, max_tree_depth);
  PE_LOG_INFO("  threads={}, batch_size={}", num_threads, batch_size);
}

// ==================== HyperParamSpace ====================

HyperParamSpace::HyperParamSpace() {
  params_ = {
      {"num_iterations", 500, 50000, 5000, 500, true, "训练迭代次数"},
      {"discount_alpha", 1.0, 3.0, 1.5, 0.1, false, "遗憾折扣指数 α"},
      {"discount_beta", 0.3, 1.0, 0.5, 0.05, false, "策略折扣指数 β"},
      {"exploit_threshold", 0.0001, 1.0, 0.001, 0.0001, false, "收敛阈值"},
      {"enable_pruning", 0, 1, 1, 1, true, "启用剪枝"},
      {"prune_regret", -1000, 0, -100, 10, false, "遗憾剪枝阈值"},
      {"prune_prob", 0, 0.1, 0.001, 0.001, false, "剪枝概率阈值"},
      {"sampling_scheme", 0, 2, 2, 1, true, "采样策略 (0/1/2)"},
      {"chance_prob", 0.1, 1.0, 1.0, 0.1, false, "机会采样率"},
      {"rollout_depth", 0, 3, 0, 1, true, "rollout深度"},
      {"epsilon_greedy", 0, 0.1, 0.0, 0.001, false, "ε-探索"},
      {"dirichlet_alpha", 0, 10, 0.3, 0.1, false, "Dirichlet噪声"},
      {"exploration_bonus", 0, 50, 0, 1, false, "UCB探索奖励"},
      {"use_rm_plus", 0, 1, 1, 1, true, "RM+"},
      {"use_discount", 0, 1, 1, 1, true, "折扣"},
      {"discount_interval", 10, 10000, 50, 10, true, "折扣间隔"},
      {"max_tree_depth", 1, 20, 10, 1, true, "最大树深"},
      {"num_threads", 1, 32, 4, 1, true, "线程数"},
      {"batch_size", 32, 8192, 1024, 32, true, "批量大小"},
  };

  for (size_t i = 0; i < params_.size(); ++i) {
    if (params_[i].step > 0) int_params_.push_back(i);
    if (params_[i].log_scale) log_params_.push_back(i);
  }
  // 布尔参数 (enable_pruning, use_rm_plus, use_discount)
  bool_params_ = {4, 13, 14};
}

CFRHyperParams HyperParamSpace::SampleRandom(std::mt19937& rng) const {
  std::vector<double> vec(params_.size());
  for (size_t i = 0; i < params_.size(); ++i) {
    vec[i] = params_[i].RandomSample(rng);
  }
  return FromNormalized(vec);
}

CFRHyperParams HyperParamSpace::FromVector(const std::vector<double>& vec) const {
  CFRHyperParams config;
  config.FromVector(vec);
  return config;
}

std::vector<double> HyperParamSpace::ToNormalized(const CFRHyperParams& config) const {
  auto raw = config.ToVector();
  std::vector<double> norm(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    if (params_[i].min_val == params_[i].max_val) {
      norm[i] = 0.5;
    } else if (params_[i].log_scale && params_[i].min_val > 0) {
      double log_min = std::log(params_[i].min_val);
      double log_max = std::log(params_[i].max_val);
      double log_val = std::log(std::max(raw[i], params_[i].min_val));
      norm[i] = (log_val - log_min) / (log_max - log_min);
    } else {
      norm[i] = (raw[i] - params_[i].min_val) / (params_[i].max_val - params_[i].min_val);
    }
  }
  return norm;
}

CFRHyperParams HyperParamSpace::FromNormalized(const std::vector<double>& norm) const {
  CFRHyperParams config;
  std::vector<double> raw(norm.size());
  for (size_t i = 0; i < norm.size(); ++i) {
    double clamped = std::max(0.0, std::min(1.0, norm[i]));
    if (params_[i].log_scale && params_[i].min_val > 0) {
      double log_min = std::log(params_[i].min_val);
      double log_max = std::log(params_[i].max_val);
      raw[i] = std::exp(log_min + clamped * (log_max - log_min));
    } else {
      raw[i] = params_[i].min_val + clamped * (params_[i].max_val - params_[i].min_val);
    }

    // 布尔参数四舍五入
    if (std::find(bool_params_.begin(), bool_params_.end(), i) != bool_params_.end()) {
      raw[i] = (raw[i] > 0.5) ? 1.0 : 0.0;
    }
  }
  config.FromVector(raw);
  return config;
}

std::pair<std::vector<double>, std::vector<double>> HyperParamSpace::Bounds() const {
  std::vector<double> lower(params_.size()), upper(params_.size());
  for (size_t i = 0; i < params_.size(); ++i) {
    lower[i] = 0.0;
    upper[i] = 1.0;
  }
  return {lower, upper};
}

}  // namespace poker_engine::cfr
