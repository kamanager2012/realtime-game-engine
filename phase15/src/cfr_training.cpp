#include "poker_engine/cfr/cfr_training.h"

#include <fmt/format.h>

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::cfr {

CFRTrainer::CFRTrainer(const CFROptions& options) : options_(options) {}

CFRNode CFRTrainer::Train(int num_iterations) {
  engine_.Initialize();
  engine_.Train(num_iterations);

  if (!engine_.NodeCount()) return CFRNode();

  return *engine_.GetNode({0, 0, 0, 0, 0});
}

void CFRTrainer::ResumeTraining(const std::string& model_path, int additional_iterations) {
  LoadModel(model_path);
  engine_.Train(additional_iterations);
}

double CFRTrainer::EvaluateExploitability() { return engine_.ComputeExploitability(); }

bool CFRTrainer::SaveModel(const std::string& filepath) {
  double exploit = engine_.ComputeExploitability();
  return CFRModelIO::Save(filepath, engine_.Nodes(), exploit);
}

bool CFRTrainer::LoadModel(const std::string& filepath) {
  std::unordered_map<uint64_t, CFRNode> nodes;
  if (!CFRModelIO::Load(filepath, nodes)) return false;

  engine_.ClearNodes();
  if (!engine_.ImportNodes(nodes)) return false;

  PE_LOG_INFO("Loaded {} nodes from {}", nodes.size(), filepath);
  return true;
}

std::string CFRTrainer::ExportStrategy(const HandAbstraction& abstraction) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3);
  oss << "# CFR+ Strategy Export\n";
  oss << "# Format: infoset_key | action:prob ...\n#\n";

  int printed = 0;
  for (auto& [key, node] : engine_.Nodes()) {
    if (node.times_visited < 10) continue;

    double avg[CFRNode::kMaxActions];
    node.get_average_strategy(avg);

    oss << "Key:" << key << " visits:" << node.times_visited << " | ";

    for (int a = 0; a < action_count(); ++a) {
      if (avg[a] > 0.001) {
        oss << action_name(static_cast<Action>(a)) << ":" << avg[a] << " ";
      }
    }
    oss << "\n";

    if (++printed >= 1000) break;
  }

  oss << "# Exported " << printed << " / " << engine_.NodeCount() << " nodes\n";
  return oss.str();
}

std::vector<std::pair<Action, double>> CFRTrainer::GetNodeStrategy(const InfosetKey& key) {
  return engine_.GetStrategy(key);
}

// ==================== AdaptiveTrainer ====================

AdaptiveTrainer::AdaptiveTrainer(const CFROptions& base_options) : options_(base_options) {}

void AdaptiveTrainer::AddCheckpoint(int iterations, const std::string& label) {
  checkpoints_.push_back({iterations, label});
}

CFRNode AdaptiveTrainer::Run(const std::string& output_path) {
  if (checkpoints_.empty()) {
    checkpoints_ = {
        {100, "initial"},
        {500, "low_regret"},
        {2000, "converged"},
    };
  }

  CFRTrainer trainer(options_);

  for (auto& [iters, label] : checkpoints_) {
    PE_LOG_INFO("=== Checkpoint: {} ({} iters) ===", label, iters);

    trainer.Train(iters);

    double exp = trainer.EvaluateExploitability();
    PE_LOG_INFO("  Exploitability: {:.6f}", exp);

    if (!output_path.empty()) {
      trainer.SaveModel(output_path + "." + label + ".cfr");
    }
  }

  return trainer.Train(options_.config.num_iterations);
}

// ==================== Policy ====================

Policy::Policy(const HandAbstraction& abstraction) {
  engine_.Initialize();
  (void)abstraction;
}

bool Policy::LoadFromFile(const std::string& filepath) {
  std::unordered_map<uint64_t, CFRNode> temp_nodes;
  if (!CFRModelIO::Load(filepath, temp_nodes)) return false;
  engine_.Initialize();
  engine_.ClearNodes();
  loaded_ = engine_.ImportNodes(temp_nodes);
  if (loaded_) PE_LOG_INFO("Policy loaded {} CFR nodes from {}", temp_nodes.size(), filepath);
  return loaded_;
}

bool Policy::LoadFromEngine(const CFREngine& engine) {
  engine_.Initialize();
  engine_.ClearNodes();
  loaded_ = engine_.ImportNodes(engine.Nodes());
  return loaded_;
}

std::vector<std::pair<Action, double>> Policy::GetActionDistribution(const game::GameState& state,
                                                                     int player) const {
  if (!loaded_) return {};
  return engine_.GetStrategyForState(state, player);
}

std::pair<Action, double> Policy::GetBestAction(const game::GameState& state, int player) const {
  auto dist = GetActionDistribution(state, player);
  if (dist.empty()) return {Action::Fold, 0.0};

  auto best = std::max_element(dist.begin(), dist.end(),
                               [](const auto& a, const auto& b) { return a.second < b.second; });
  return *best;
}

void Policy::GetActionsBatch(const std::vector<InfosetKey>& keys,
                             std::vector<std::vector<std::pair<Action, double>>>& results) const {
  results.resize(keys.size());
  for (size_t i = 0; i < keys.size(); ++i) {
    results[i] = const_cast<CFREngine&>(engine_).GetStrategy(keys[i]);
  }
}

}  // namespace poker_engine::cfr
