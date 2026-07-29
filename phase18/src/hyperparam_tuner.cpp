#include "poker_engine/cfr/hyperparam_tuner.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <random>
#include <sstream>
#include <thread>

#include "poker_engine/base/logging.h"

namespace poker_engine::cfr {

HyperParamTuner::HyperParamTuner(const TunerConfig& config) : config_(config) {
#ifdef HAVE_EIGEN3
  bayesian_opt_ = new BayesianOptimizer(param_space_);
  sobal_ = new SobolAnalysis(static_cast<int>(param_space_.Size()));
#endif
}

HyperParamTuner::~HyperParamTuner() {
#ifdef HAVE_EIGEN3
  delete bayesian_opt_;
  delete sobal_;
#endif
}

void HyperParamTuner::RunSensitivityAnalysis() {
  PE_LOG_INFO("=== Phase 1: Sensitivity Analysis ===");
  PE_LOG_INFO("Generating {} Sobol samples", config_.sobol_samples);

#ifdef HAVE_EIGEN3
  sobal_->GenerateSamples(config_.sobol_samples, param_space_);
  // Evaluate each sample
  std::vector<double> outputs;
  int total_trials = config_.sobol_samples * (2 * static_cast<int>(param_space_.Size()) + 2);
  PE_LOG_INFO("Evaluating {} configurations...", total_trials);

  for (int i = 0; i < total_trials; ++i) {
    std::mt19937 trial_rng(42 + i);
    auto config = param_space_.SampleRandom(trial_rng);
    auto result = EvaluateConfig(config);
    outputs.push_back(result.exploitability);
    trial_results_.push_back(result);
    if ((i + 1) % 20 == 0) {
      PE_LOG_INFO("  Evaluated {}/{} configs", i + 1, total_trials);
    }
  }

  sobal_->ComputeIndices(outputs);
  sobal_->PrintResults();

  auto ranked = sobal_->RankedParameters();
  for (int idx : ranked) {
    SensitivityResult sr;
    sr.param_index = idx;
    sr.param_name = param_space_.Params()[idx].name;
    sr.first_order = sobal_->FirstOrderIndices()[idx];
    sr.total_order = sobal_->TotalOrderIndices()[idx];
    sensitivity_results_.push_back(sr);
  }
#else
  // Fallback: one-at-a-time sensitivity without Sobol
  PE_LOG_INFO("Sobol analysis unavailable (no Eigen3), using one-at-a-time");
  auto params = param_space_.Params();
  for (size_t idx = 0; idx < params.size(); ++idx) {
    SensitivityResult sr;
    sr.param_index = static_cast<int>(idx);
    sr.param_name = params[idx].name;
    sensitivity_results_.push_back(sr);
  }
#endif

  PE_LOG_INFO("Sensitivity analysis complete");
}

void HyperParamTuner::RunBayesianOptimization() {
  PE_LOG_INFO("=== Phase 2: Bayesian Optimization ===");

#ifdef HAVE_EIGEN3
  bayesian_opt_->Optimize(config_.bayesian_trials);
  best_config_ = bayesian_opt_->BestConfig();
  best_exploitability_ = bayesian_opt_->BestScore();

  PE_LOG_INFO("Bayesian optimization found best exploitability: {:.6f}", best_exploitability_);
  best_config_.Print();

  for (auto& trial : bayesian_opt_->History()) {
    trial_results_.push_back(TrainingResult{trial.config, trial.exploitability, 0.0,
                                            trial.time_seconds, trial.nodes, 0.0, 0, 0.0, 0.0});
  }
#else
  // Fallback: random search
  PE_LOG_INFO("Bayesian optimization unavailable (no Eigen3), using random search");
  std::mt19937 rng(42);
  for (int i = 0; i < config_.bayesian_trials; ++i) {
    auto config = param_space_.SampleRandom(rng);
    auto result = EvaluateConfig(config);
    trial_results_.push_back(result);
    if (result.exploitability < best_exploitability_) {
      best_exploitability_ = result.exploitability;
      best_config_ = config;
    }
    if ((i + 1) % 10 == 0) {
      PE_LOG_INFO("  Trial {}/{}: best={:.6f}", i + 1, config_.bayesian_trials,
                  best_exploitability_);
    }
  }
  PE_LOG_INFO("Random search found best exploitability: {:.6f}", best_exploitability_);
#endif
}

CFRHyperParams HyperParamTuner::FineTune(const CFRHyperParams& baseline, double search_radius) {
  PE_LOG_INFO("=== Phase 3: Fine-Tuning ===");

  CFRHyperParams best = baseline;
  double best_exploit = EvaluateConfig(best).exploitability;

  auto all_params = param_space_.Params();
  for (size_t p = 0; p < all_params.size(); ++p) {
    double orig_val = baseline.ToVector()[p];
    double lo = std::max(0.0, orig_val - search_radius);
    double hi = std::min(1.0, orig_val + search_radius);

    for (int s = -1; s <= 1; ++s) {
      double test_val = orig_val + s * search_radius * 0.33;
      test_val = std::max(lo, std::min(hi, test_val));

      CFRHyperParams test_config = baseline;
      auto vec = test_config.ToVector();
      vec[p] = test_val;
      test_config.FromVector(vec);

      auto result = EvaluateConfig(test_config);
      if (result.exploitability < best_exploit) {
        best_exploit = result.exploitability;
        best = test_config;
      }
    }
  }

  PE_LOG_INFO("Fine-tuning improved exploitability: {:.6f} -> {:.6f}", best_exploitability_,
              best_exploit);

  best_config_ = best;
  best_exploitability_ = best_exploit;
  return best;
}

TrainingResult HyperParamTuner::EvaluateConfig(const CFRHyperParams& config) {
  TrainingResult result;
  result.config = config;
  result.exploitability = 0.1;  // Placeholder — should call CFREngine
  result.time_seconds = 0.5;
  result.node_count = 1000;
  result.iterations_run = config_.cfr_iterations_per_trial;
  result.samples_per_sec = 0;
  result.convergence_rate = 0;
  result.quality_score = result.exploitability * (1.0 + 0.1 * std::log(1.0 + result.time_seconds));
  return result;
}

CFRHyperParams HyperParamTuner::BestConfig() const { return best_config_; }
double HyperParamTuner::BestExploitability() const { return best_exploitability_; }

std::string HyperParamTuner::GenerateReport() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(6);
  oss << "================================================================\n";
  oss << "              CFR Hyperparameter Tuning Report\n";
  oss << "================================================================\n\n";
  oss << "Best Exploitability: " << best_exploitability_ << "\n";
  oss << "Total Trials: " << trial_results_.size() << "\n\n";

  if (!sensitivity_results_.empty()) {
    oss << "Sensitivity Analysis:\n";
    oss << "------------------------------------------------------------\n";
    for (auto& sr : sensitivity_results_) {
      oss << "  " << sr.param_name << "\n";
    }
  }

  oss << "================================================================\n";
  return oss.str();
}

bool HyperParamTuner::SaveProgress(const std::string& path) const {
  try {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    ofs << "{\n  \"best_exploitability\": " << best_exploitability_ << "\n}\n";
    PE_LOG_INFO("Tuning progress saved to {}", path);
    return true;
  } catch (...) {
    return false;
  }
}

bool HyperParamTuner::LoadProgress(const std::string& path) {
  try {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;
    PE_LOG_INFO("Progress loading from {} (stub)", path);
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace poker_engine::cfr
