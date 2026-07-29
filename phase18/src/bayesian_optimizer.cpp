#include "poker_engine/cfr/bayesian_optimizer.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "poker_engine/base/logging.h"

namespace poker_engine::cfr {

using Matrix = GaussianProcess::Matrix;
using Vector = GaussianProcess::Vector;

// ==================== GaussianProcess ====================

GaussianProcess::GaussianProcess(double noise_variance, double length_scale, double signal_variance)
    : length_scale_(length_scale),
      signal_variance_(signal_variance),
      noise_variance_(noise_variance) {}

double GaussianProcess::Kernel(const Vector& a, const Vector& b) const {
  // Matern 2.5 核 (比 RBF 更适合超参数优化)
  double dist = (a - b).norm() / length_scale_;
  double sqrt3 = std::sqrt(3.0);
  double sqrt5 = std::sqrt(5.0);

  double val = 1.0 + sqrt3 * dist + (1.0 / 3.0) * (3.0 * dist * dist + sqrt5 * std::pow(dist, 3));

  return signal_variance_ * val * std::exp(-sqrt3 * dist);
}

Matrix GaussianProcess::PairwiseDistances(const Matrix& X1, const Matrix& X2) const {
  int n1 = X1.rows();
  int n2 = X2.rows();
  Matrix dists(n1, n2);

  for (int i = 0; i < n1; ++i) {
    for (int j = 0; j < n2; ++j) {
      dists(i, j) = (X1.row(i) - X2.row(j)).squaredNorm();
    }
  }

  return dists;
}

Matrix GaussianProcess::ComputeKernelMatrix(const Matrix& X1, const Matrix& X2) const {
  Matrix dists = PairwiseDistances(X1, X2);
  Matrix K(dists.rows(), dists.cols());

  for (int i = 0; i < dists.rows(); ++i) {
    for (int j = 0; j < dists.cols(); ++j) {
      double dist = std::sqrt(dists(i, j)) / length_scale_;
      // Matern 2.5
      double s3 = std::sqrt(3.0) * dist;
      double s5 = std::sqrt(5.0) * dist;
      K(i, j) = signal_variance_ * (1.0 + s3 + (1.0 / 3.0) * s3 * s3) * std::exp(-s3);
    }
  }

  return K;
}

void GaussianProcess::AddObservation(const Vector& x, double y) {
  if (X_obs_.rows() == 0) {
    X_obs_ = x.transpose();
    y_obs_ = Vector::Constant(1, y);
  } else {
    int n = X_obs_.rows();
    Matrix new_X(n + 1, x.size());
    new_X << X_obs_, x.transpose();
    X_obs_ = new_X;

    Vector new_y(n + 1);
    new_y << y_obs_, Vector::Constant(1, y);
    y_obs_ = new_y;
  }

  needs_update_ = true;
}

void GaussianProcess::UpdateInverse() {
  if (!needs_update_ || X_obs_.rows() == 0) return;

  int n = X_obs_.rows();

  // 计算协方差矩阵 + 噪声
  Matrix K = ComputeKernelMatrix(X_obs_, X_obs_);
  K += Matrix::Identity(n, n) * noise_variance_;

  // 使用 Cholesky 分解求逆 (数值稳定)
  Eigen::LLT<Matrix> llt(K);
  if (llt.info() == Eigen::Success) {
    K_inv_ = llt.solve(Matrix::Identity(n, n));
  } else {
    // 回退到伪逆
    K_inv_ = K.completeOrthogonalDecomposition().pseudoInverse();
    PE_LOG_WARN("GP: Cholesky failed, using pseudo-inverse");
  }

  needs_update_ = false;
}

std::pair<double, double> GaussianProcess::Predict(const Vector& x) const {
  if (X_obs_.rows() == 0) return {0.0, signal_variance_};

  // 需要 const_cast 因为 Eigen 的 API 不够 const-friendly
  const_cast<GaussianProcess*>(this)->UpdateInverse();

  int n = X_obs_.rows();

  // k* = K(X_obs, x)
  Vector k_star(n);
  for (int i = 0; i < n; ++i) {
    k_star(i) = Kernel(X_obs_.row(i), x);
  }

  // 均值 = k*^T K^{-1} y
  double mean = k_star.dot(K_inv_ * y_obs_);

  // 方差 = k(x,x) - k*^T K^{-1} k*
  double k_self = Kernel(x, x);
  double variance = k_self - k_star.dot(K_inv_ * k_star);
  variance = std::max(1e-10, variance);  // 数值安全

  return {mean, variance};
}

void GaussianProcess::PredictBatch(const Matrix& X, Vector& means, Vector& variances) const {
  int m = X.rows();
  means.resize(m);
  variances.resize(m);

  for (int i = 0; i < m; ++i) {
    auto [mu, var] = Predict(X.row(i));
    means(i) = mu;
    variances(i) = var;
  }
}

void GaussianProcess::Clear() {
  X_obs_.resize(0, 0);
  y_obs_.resize(0);
  K_inv_.resize(0, 0);
  needs_update_ = true;
}

// ==================== Acquisition ====================

Vector Acquisition::ExpectedImprovement(const GaussianProcess& gp,
                                        const Eigen::MatrixXd& candidates, double best_y,
                                        double exploration) {
  int n = candidates.rows();
  Vector eis(n);

  for (int i = 0; i < n; ++i) {
    auto [mu, sigma] = gp.Predict(candidates.row(i));
    sigma = std::sqrt(std::max(1e-10, sigma));

    double improvement = best_y - mu - exploration;
    double z = improvement / sigma;

    // EI = improvement * Phi(z) + sigma * phi(z)
    double phi = std::exp(-0.5 * z * z) / std::sqrt(2.0 * M_PI);
    double Phi = 0.5 * (1.0 + std::erf(z / std::sqrt(2.0)));

    eis(i) = improvement * Phi + sigma * phi;
    eis(i) = std::max(0.0, eis(i));
  }

  return eis;
}

Vector Acquisition::UpperConfidenceBound(const GaussianProcess& gp,
                                         const Eigen::MatrixXd& candidates, double kappa) {
  int n = candidates.rows();
  Vector ucbs(n);

  for (int i = 0; i < n; ++i) {
    auto [mu, sigma] = gp.Predict(candidates.row(i));
    ucbs(i) = mu - kappa * std::sqrt(std::max(1e-10, sigma));
    // 负的 exploitability 更好，所以取 mu - kappa*sigma
  }

  return ucbs;
}

Vector Acquisition::ProbabilityOfImprovement(const GaussianProcess& gp,
                                             const Eigen::MatrixXd& candidates, double best_y,
                                             double xi) {
  int n = candidates.rows();
  Vector pis(n);

  for (int i = 0; i < n; ++i) {
    auto [mu, sigma] = gp.Predict(candidates.row(i));
    sigma = std::sqrt(std::max(1e-10, sigma));

    double z = (best_y - mu - xi) / sigma;
    pis(i) = 0.5 * (1.0 + std::erf(z / std::sqrt(2.0)));
  }

  return pis;
}

// ==================== BayesianOptimizer ====================

BayesianOptimizer::BayesianOptimizer(const HyperParamSpace& space,
                                     const BayesianOptimizerConfig& config)
    : space_(space), config_(config), rng_(config.random_init) {}

Eigen::VectorXd BayesianOptimizer::ParamsToVector(const CFRHyperParams& params) const {
  auto raw = params.ToVector();
  auto bounds = space_.Bounds();
  Eigen::VectorXd vec(raw.size());

  for (size_t i = 0; i < raw.size(); ++i) {
    double lo = bounds.first[i];   // 0.0
    double hi = bounds.second[i];  // 1.0 (归一化)
    vec(i) = (raw[i] - lo) / (hi - lo);
    vec(i) = std::max(0.0, std::min(1.0, vec(i)));
  }

  return vec;
}

CFRHyperParams BayesianOptimizer::VectorToParams(const Eigen::VectorXd& vec) const {
  std::vector<double> raw(vec.size());
  for (int i = 0; i < vec.size(); ++i) {
    raw[i] = vec(i);
  }
  return space_.FromNormalized(raw);
}

void BayesianOptimizer::Optimize(int total_trials) {
  PE_LOG_INFO("Bayesian Optimization: {} trials, {} random init", total_trials,
              config_.random_init);

  auto& params_def = space_.Params();
  int dim = params_def.size();

  // Step 1: 随机初始化
  std::uniform_real_distribution<double> dist(0.0, 1.0);

  for (int i = 0; i < config_.random_init && i < total_trials; ++i) {
    Eigen::VectorXd x(dim);
    for (int d = 0; d < dim; ++d) {
      x(d) = dist(rng_);
    }
    auto config = VectorToParams(x);

    // 运行 CFR 训练 (需要外部评估器)
    // 这里只记录配置，实际评估由外部调用者完成
    TrialResult result;
    result.config = config;
    result.exploitability = 0.0;
    result.time_seconds = 0.0;
    result.nodes = 0;

    AddResult(result);
    gp_.AddObservation(x, result.exploitability);

    PE_LOG_INFO("Random init {}/{}: exploit={:.6f}, time={:.1f}s", i + 1, config_.random_init,
                result.exploitability, result.time_seconds);
  }

  // Step 2: 贝叶斯优化循环
  for (int iter = config_.random_init; iter < total_trials; ++iter) {
    // 建议下一个试验点
    Eigen::VectorXd next_x = OptimizeAcquisition(5);
    auto next_config = VectorToParams(next_x);

    next_config.Print();

    TrialResult result;
    result.config = next_config;
    result.exploitability = 0.0;
    result.time_seconds = 0.0;
    result.nodes = 0;

    AddResult(result);
    gp_.AddObservation(next_x, result.exploitability);

    PE_LOG_INFO("BO iter {}/{}: exploit={:.6f}, time={:.1f}s, best={:.6f}", iter + 1, total_trials,
                result.exploitability, result.time_seconds, BestScore());

    // 收敛检测
    if (config_.early_stop && HasConverged()) {
      PE_LOG_INFO("Converged at iteration {}", iter + 1);
      break;
    }
  }

  PE_LOG_INFO("Optimization complete. Best exploit: {:.6f}", BestScore());
  PE_LOG_INFO("Best config:");
  BestConfig().Print();
}

CFRHyperParams BayesianOptimizer::SuggestNext() {
  if (history_.empty()) {
    return space_.SampleRandom(rng_);
  }

  auto& params_def = space_.Params();
  int dim = params_def.size();

  // 如果观测不足，随机采样
  if (static_cast<int>(gp_.NumObservations()) < config_.random_init) {
    Eigen::VectorXd x(dim);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (int d = 0; d < dim; ++d) x(d) = dist(rng_);
    return VectorToParams(x);
  }

  Eigen::VectorXd next_x = OptimizeAcquisition(10);
  return VectorToParams(next_x);
}

void BayesianOptimizer::AddResult(const TrialResult& result) { history_.push_back(result); }

bool BayesianOptimizer::HasConverged() const {
  if (history_.size() < static_cast<size_t>(config_.converge_patience)) {
    return false;
  }

  // 检查最近 N 次试验的最佳 exploit 变化
  double best_recent = std::numeric_limits<double>::max();
  size_t start = history_.size() - config_.converge_patience;

  for (size_t i = start; i < history_.size(); ++i) {
    best_recent = std::min(best_recent, history_[i].exploitability);
  }

  double prev_best = std::numeric_limits<double>::max();
  for (size_t i = 0; i < start; ++i) {
    prev_best = std::min(prev_best, history_[i].exploitability);
  }

  return std::abs(best_recent - prev_best) < config_.converge_threshold;
}

CFRHyperParams BayesianOptimizer::BestConfig() const {
  if (history_.empty()) return space_.SampleRandom(rng_);

  size_t best_idx = 0;
  double best_score = history_[0].exploitability;

  for (size_t i = 1; i < history_.size(); ++i) {
    if (history_[i].exploitability < best_score) {
      best_score = history_[i].exploitability;
      best_idx = i;
    }
  }

  return history_[best_idx].config;
}

double BayesianOptimizer::BestScore() const {
  if (history_.empty()) return std::numeric_limits<double>::max();

  double best = history_[0].exploitability;
  for (const auto& h : history_) {
    best = std::min(best, h.exploitability);
  }
  return best;
}

Eigen::VectorXd BayesianOptimizer::OptimizeAcquisition(int num_restarts) {
  int dim = space_.Size();
  Eigen::VectorXd best_x(dim);
  double best_acq = std::numeric_limits<double>::lowest();

  double best_y_so_far = BestScore();

  for (int restart = 0; restart < num_restarts; ++restart) {
    // 随机起点
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    Eigen::VectorXd x(dim);
    for (int d = 0; d < dim; ++d) x(d) = dist(rng_);

    // 梯度上升 (简化：L-BFGS 风格)
    double step_size = 0.1;
    for (int iter = 0; iter < 50; ++iter) {
      // 计算采集函数值和数值梯度
      auto [mu, var] = gp_.Predict(x);
      double sigma = std::sqrt(std::max(1e-10, var));

      double acq_value;
      Eigen::VectorXd grad = Eigen::VectorXd::Zero(dim);

      switch (config_.acq_func) {
        case AcquisitionFunction::ExpectedImprovement: {
          double improvement = best_y_so_far - mu - config_.exploration;
          double z = improvement / sigma;
          double phi = std::exp(-0.5 * z * z) / std::sqrt(2.0 * M_PI);
          double Phi = 0.5 * (1.0 + std::erf(z / std::sqrt(2.0)));

          acq_value = improvement * Phi + sigma * phi;

          // 梯度 (数值近似)
          for (int d = 0; d < dim; ++d) {
            double eps = 1e-6;
            Eigen::VectorXd xp = x, xm = x;
            xp(d) += eps;
            xm(d) -= eps;

            auto [mu_p, var_p] = gp_.Predict(xp);
            auto [mu_m, var_m] = gp_.Predict(xm);
            double sigma_p = std::sqrt(std::max(1e-10, var_p));
            double sigma_m = std::sqrt(std::max(1e-10, var_m));

            double imp_p = best_y_so_far - mu_p - config_.exploration;
            double imp_m = best_y_so_far - mu_m - config_.exploration;
            double z_p = imp_p / sigma_p;
            double z_m = imp_m / sigma_m;

            double phi_p = std::exp(-0.5 * z_p * z_p) / std::sqrt(2.0 * M_PI);
            double phi_m = std::exp(-0.5 * z_m * z_m) / std::sqrt(2.0 * M_PI);
            double Phi_p = 0.5 * (1.0 + std::erf(z_p / std::sqrt(2.0)));
            double Phi_m = 0.5 * (1.0 + std::erf(z_m / std::sqrt(2.0)));

            double acq_p = imp_p * Phi_p + sigma_p * phi_p;
            double acq_m = imp_m * Phi_m + sigma_m * phi_m;

            grad(d) = (acq_p - acq_m) / (2 * eps);
          }
          break;
        }
        case AcquisitionFunction::UpperConfidenceBound: {
          acq_value = mu - config_.kappa * sigma;

          // UCB 梯度 = mu_grad - kappa * sigma_grad
          for (int d = 0; d < dim; ++d) {
            double eps = 1e-6;
            Eigen::VectorXd xp = x, xm = x;
            xp(d) += eps;
            xm(d) -= eps;

            auto [mu_p, var_p] = gp_.Predict(xp);
            auto [mu_m, var_m] = gp_.Predict(xm);

            double sigma_p = std::sqrt(std::max(1e-10, var_p));
            double sigma_m = std::sqrt(std::max(1e-10, var_m));

            grad(d) =
                ((mu_p - config_.kappa * sigma_p) - (mu_m - config_.kappa * sigma_m)) / (2 * eps);
          }
          break;
        }
        case AcquisitionFunction::ProbabilityOfImprovement: {
          double z = (best_y_so_far - mu - 0.01) / sigma;
          acq_value = 0.5 * (1.0 + std::erf(z / std::sqrt(2.0)));
          // PI 梯度
          double phi_z = std::exp(-0.5 * z * z) / std::sqrt(2.0 * M_PI);
          for (int d = 0; d < dim; ++d) {
            double eps = 1e-6;
            Eigen::VectorXd xp = x, xm = x;
            xp(d) += eps;
            xm(d) -= eps;

            auto [mu_p, var_p] = gp_.Predict(xp);
            auto [mu_m, var_m] = gp_.Predict(xm);

            double sigma_p = std::sqrt(std::max(1e-10, var_p));
            double sigma_m = std::sqrt(std::max(1e-10, var_m));
            double z_p = (best_y_so_far - mu_p - 0.01) / sigma_p;
            double z_m = (best_y_so_far - mu_m - 0.01) / sigma_m;

            double pi_p = 0.5 * (1.0 + std::erf(z_p / std::sqrt(2.0)));
            double pi_m = 0.5 * (1.0 + std::erf(z_m / std::sqrt(2.0)));

            grad(d) = (pi_p - pi_m) / (2 * eps);
          }
          break;
        }
      }

      // 梯度上升更新
      x = x + step_size * grad.normalized();

      // 投影回 [0, 1]^d
      x = x.cwiseMax(0.0).cwiseMin(1.0);

      step_size *= 0.99;  // 衰减步长

      double current_acq = acq_value;
      if (current_acq > best_acq) {
        best_acq = current_acq;
        best_x = x;
      }
    }
  }

  return best_x;
}

std::vector<size_t> BayesianOptimizer::ParetoFront() const {
  std::vector<size_t> front;

  for (size_t i = 0; i < history_.size(); ++i) {
    bool dominated = false;
    for (size_t j = 0; j < history_.size(); ++j) {
      if (i == j) continue;
      // j dominates i if j is better on all objectives
      bool j_better_exploit = history_[j].exploitability <= history_[i].exploitability;
      bool j_better_cost = history_[j].Cost() <= history_[i].Cost();
      bool j_strictly_better = (history_[j].exploitability < history_[i].exploitability) ||
                               (history_[j].Cost() < history_[i].Cost());

      if (j_better_exploit && j_better_cost && j_strictly_better) {
        dominated = true;
        break;
      }
    }
    if (!dominated) front.push_back(i);
  }

  return front;
}

}  // namespace poker_engine::cfr
