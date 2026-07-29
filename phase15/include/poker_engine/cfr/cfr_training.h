#pragma once

#include <functional>
#include <memory>
#include <string>

#include "cfr_engine.h"
#include "cfr_model.h"

namespace poker_engine::cfr {

using TrainingCallback =
    std::function<void(int iteration, double exploitability, size_t nodes, double elapsed_ms)>;

class CFRTrainer {
 public:
  explicit CFRTrainer(const CFROptions& options = CFROptions());

  void SetIterations(int n) {
    options_.config.num_iterations = n;
    engine_.Config().num_iterations = n;
  }
  void SetDiscountInterval(double d) {
    options_.config.discount_interval = d;
    engine_.Config().discount_interval = d;
  }
  void SetExploitabilityThreshold(double t) {
    options_.config.exploitability_threshold = t;
    engine_.Config().exploitability_threshold = t;
  }
  void EnablePruning(bool enable) {
    options_.enable_pruning = enable;
    engine_.SetPruning(enable);
  }

  CFRNode Train(int num_iterations = 1000);
  void ResumeTraining(const std::string& model_path, int additional_iterations);

  double EvaluateExploitability();

  bool SaveModel(const std::string& filepath);
  bool LoadModel(const std::string& filepath);

  size_t NodeCount() const { return engine_.NodeCount(); }
  const CFREngine& Engine() const { return engine_; }
  CFREngine& Engine() { return engine_; }

  std::string ExportStrategy(const HandAbstraction& abstraction);
  std::vector<std::pair<Action, double>> GetNodeStrategy(const InfosetKey& key);

 private:
  CFROptions options_;
  CFREngine engine_;
  TrainingCallback callback_;
};

class AdaptiveTrainer {
 public:
  AdaptiveTrainer(const CFROptions& base_options = CFROptions());

  CFRNode Run(const std::string& output_path = "");
  void AddCheckpoint(int iterations, const std::string& label = "");

 private:
  CFROptions options_;
  std::vector<std::pair<int, std::string>> checkpoints_;
};

class Policy {
 public:
  Policy() = default;
  explicit Policy(const HandAbstraction& abstraction);

  bool LoadFromFile(const std::string& filepath);
  bool LoadFromEngine(const CFREngine& engine);

  std::vector<std::pair<Action, double>> GetActionDistribution(const game::GameState& state,
                                                               int player) const;

  std::pair<Action, double> GetBestAction(const game::GameState& state, int player) const;

  void GetActionsBatch(const std::vector<InfosetKey>& keys,
                       std::vector<std::vector<std::pair<Action, double>>>& results) const;

  bool IsValid() const { return loaded_; }
  size_t NodeCount() const { return engine_.NodeCount(); }
  const CFREngine& Engine() const { return engine_; }

 private:
  CFREngine engine_;
  bool loaded_ = false;
};

}  // namespace poker_engine::cfr
