#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cfr_abstraction.h"
#include "cfr_engine.h"
#include "cfr_node.h"
#include "cfr_range.h"
#include "types.h"

namespace poker_engine::cfr {

struct PublicTreeNode {
  uint64_t id = 0;
  Street street = Street::Preflop;
  BoardCards board;
  uint16_t pot_size = 0;
  bool is_terminal = false;

  std::vector<std::pair<Action, PublicTreeNode*>> children;
  PublicTreeNode* parent = nullptr;

  CFRNode strategy_node;

  std::vector<uint16_t> hand_buckets;
};

class PublicTree {
 public:
  PublicTree();

  void Build(int max_street = static_cast<int>(Street::River), int max_bets_per_round = 2);

  size_t NodeCount() const { return nodes_.size(); }
  size_t TerminalCount() const;

  PublicTreeNode* Root() { return &nodes_[0]; }
  const PublicTreeNode* Root() const { return &nodes_[0]; }

  void PrintStats() const;

  const HandAbstraction& GetAbstraction() const { return abstraction_; }
  void SetAbstraction(const HandAbstraction& abs) { abstraction_ = abs; }

 private:
  PublicTreeNode* CreateNode(Street street, const BoardCards& board, uint16_t pot, bool terminal);
  void BuildSubtree(PublicTreeNode* node, Street current_street, int bets_remaining,
                    int max_street);
  void AssignBuckets(PublicTreeNode* node);

  std::vector<PublicTreeNode> nodes_;
  uint64_t next_id_ = 0;
  HandAbstraction abstraction_;
  int max_bets_per_round_ = 2;
};

struct SolverOptions {
  CFROptions cfr_options;

  int num_iterations = 10000;
  int check_interval = 500;
  double target_exploitability = 0.001;
  bool use_cfr_plus = true;
  bool use_chance_sampling = true;
  double sampling_rate = 1.0;
  int snapshot_interval = 0;
  std::string snapshot_prefix;
};

class PublicTreeSolver {
 public:
  explicit PublicTreeSolver(const SolverOptions& options = SolverOptions());

  void Initialize(PublicTree& tree);

  void Solve(int num_iterations = 0);

  double Traverse(PublicTreeNode* node, const Range& hero_range, const Range& villain_range,
                  int player, double reach_prob);

  double CSTraverse(PublicTreeNode* node, const Range& hero_range, const Range& villain_range,
                    int player);

  double ComputeExploitability();

  void GetStrategy(PublicTreeNode* node, uint16_t bucket, double out[kMaxActions]) const;

  void GetStrategyAll(PublicTreeNode* node, std::vector<std::vector<double>>& strategies) const;

  bool SaveSnapshot(const std::string& filepath);
  bool LoadSnapshot(const std::string& filepath);

  const SolverOptions& Options() const { return options_; }
  size_t IterationCount() const { return iteration_; }
  double CurrentExploitability() const { return exploitability_; }

  struct SolveStats {
    int iterations = 0;
    double final_exploitability = 0.0;
    size_t total_nodes = 0;
    size_t terminal_nodes = 0;
    double elapsed_ms = 0.0;
    double nash_distance = 0.0;
    std::string ToString() const;
  };
  SolveStats GetStats() const { return stats_; }

 private:
  void TrainIteration(PublicTree& tree);
  double TerminalValue(PublicTreeNode* node, int player, const Range& hero_range,
                       const Range& villain_range) const;
  void ApplyDiscount();

  SolverOptions options_;
  PublicTree* tree_ = nullptr;
  HandAbstraction abstraction_;

  size_t iteration_ = 0;
  double exploitability_ = 0.0;
  SolveStats stats_;

  std::mt19937 rng_{42};
};

}  // namespace poker_engine::cfr
