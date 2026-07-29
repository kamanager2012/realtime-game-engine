#include "poker_engine/cfr/public_tree_solver.h"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <queue>

#include "poker_engine/base/logging.h"
#include "poker_engine/cfr/cfr_model.h"

namespace poker_engine::cfr {

// ==================== PublicTree ====================

PublicTree::PublicTree() {}

PublicTreeNode* PublicTree::CreateNode(Street street, const BoardCards& board, uint16_t pot,
                                       bool terminal) {
  nodes_.emplace_back();
  PublicTreeNode& node = nodes_.back();
  node.id = next_id_++;
  node.street = street;
  node.board = board;
  node.pot_size = pot;
  node.is_terminal = terminal;
  return &nodes_.back();
}

void PublicTree::Build(int max_street, int max_bets_per_round) {
  nodes_.clear();
  nodes_.reserve(16384);  // prevent pointer invalidation during subtree expansion
  next_id_ = 0;
  max_bets_per_round_ = max_bets_per_round;

  PublicTreeNode* root = CreateNode(Street::Preflop, BoardCards{}, 0, false);
  BuildSubtree(root, Street::Preflop, max_bets_per_round, max_street);
  AssignBuckets(root);

  PE_LOG_INFO("PublicTree built: {} nodes ({} terminal, max_street={}, max_bets={}", nodes_.size(),
              TerminalCount(), max_street, max_bets_per_round);
}

void PublicTree::BuildSubtree(PublicTreeNode* node, Street current_street, int bets_remaining,
                              int max_street) {
  if (node->is_terminal) return;

  int street_idx = static_cast<int>(current_street);
  if (street_idx > max_street) {
    node->is_terminal = true;
    return;
  }

  if (bets_remaining <= 0 && street_idx > 0) {
    BoardCards next_board = node->board;
    Street next_street = static_cast<Street>(street_idx + 1);

    if (static_cast<int>(next_street) <= max_street) {
      PublicTreeNode* child = CreateNode(next_street, next_board, node->pot_size, false);
      node->children.push_back({Action::Call, child});
      child->parent = node;
      BuildSubtree(child, next_street, max_bets_per_round_, max_street);
    } else {
      node->is_terminal = true;
    }
    return;
  }

  Action actions[] = {Action::Fold, Action::Call, Action::BetHalf, Action::BetPot};
  int num_actions = (bets_remaining > 0) ? 4 : 2;

  for (int i = 0; i < num_actions; ++i) {
    Action action = actions[i];
    bool is_terminal = false;
    uint16_t new_pot = node->pot_size;
    int new_bets = bets_remaining;
    Street next_street = current_street;

    switch (action) {
      case Action::Fold:
        is_terminal = true;
        break;
      case Action::Call:
        if (street_idx < static_cast<int>(Street::River)) {
          next_street = static_cast<Street>(street_idx + 1);
          new_bets = max_bets_per_round_;
        } else {
          is_terminal = true;
        }
        break;
      case Action::BetHalf:
        new_pot = node->pot_size + static_cast<uint16_t>(node->pot_size / 2);
        new_bets = bets_remaining - 1;
        break;
      case Action::BetPot:
        new_pot = node->pot_size * 2;
        new_bets = bets_remaining - 1;
        break;
      default:
        break;
    }

    BoardCards next_board = node->board;
    if (action == Action::Call && !is_terminal) {
      switch (next_street) {
        case Street::Flop:
          next_board.count = 3;
          break;
        case Street::Turn:
          next_board.count = 4;
          break;
        case Street::River:
          next_board.count = 5;
          break;
        default:
          break;
      }
    }

    PublicTreeNode* child = CreateNode(next_street, next_board, new_pot, is_terminal);
    node->children.push_back({action, child});
    child->parent = node;

    if (!is_terminal) {
      BuildSubtree(child, next_street, new_bets, max_street);
    }
  }
}

void PublicTree::AssignBuckets(PublicTreeNode* node) {
  if (!node) return;

  if (node->street == Street::Preflop) {
    node->hand_buckets.resize(HandAbstraction::kNumBuckets);
    std::iota(node->hand_buckets.begin(), node->hand_buckets.end(), 0);
  } else {
    int num_buckets =
        std::max(1, HandAbstraction::kNumBuckets / (1 << (2 * static_cast<int>(node->street))));
    node->hand_buckets.resize(num_buckets);
    std::iota(node->hand_buckets.begin(), node->hand_buckets.end(), 0);
  }

  for (auto& [action, child] : node->children) {
    AssignBuckets(child);
  }
}

size_t PublicTree::TerminalCount() const {
  size_t count = 0;
  for (auto& node : nodes_) {
    if (node.is_terminal) count++;
  }
  return count;
}

void PublicTree::PrintStats() const {
  size_t terminal = TerminalCount();
  size_t by_street[5] = {};
  for (auto& node : nodes_) {
    int s = static_cast<int>(node.street);
    if (s >= 0 && s < 5) by_street[s]++;
  }

  PE_LOG_INFO("=== PublicTree Stats ===");
  PE_LOG_INFO("  Total nodes: {}", nodes_.size());
  PE_LOG_INFO("  Terminal: {}", terminal);
  for (int i = 0; i < 5; ++i) {
    const char* names[] = {"Preflop", "Flop", "Turn", "River", "Terminal"};
    PE_LOG_INFO("  {}: {} nodes", names[i], by_street[i]);
  }
}

// ==================== PublicTreeSolver ====================

PublicTreeSolver::PublicTreeSolver(const SolverOptions& options) : options_(options) {}

void PublicTreeSolver::Initialize(PublicTree& tree) {
  tree_ = &tree;
  abstraction_ = tree.GetAbstraction();
  iteration_ = 0;
  exploitability_ = 0.0;
  stats_ = SolveStats{};

  PE_LOG_INFO("PublicTreeSolver initialized: {} nodes", tree.NodeCount());
}

void PublicTreeSolver::Solve(int num_iterations) {
  if (!tree_) {
    PE_LOG_ERROR("No tree initialized");
    return;
  }

  int iters = num_iterations > 0 ? num_iterations : options_.num_iterations;
  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < iters; ++i) {
    TrainIteration(*tree_);
    iteration_++;

    if (options_.use_cfr_plus && iteration_ % 100 == 0) {
      ApplyDiscount();
    }

    if (i > 0 && i % options_.check_interval == 0) {
      exploitability_ = ComputeExploitability();

      auto elapsed = std::chrono::steady_clock::now() - start;
      double ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

      PE_LOG_INFO("Solve iter {}: exploitability={:.6f}, nodes={}, {:.0f}ms", i, exploitability_,
                  tree_->NodeCount(), ms);

      if (exploitability_ < options_.target_exploitability) {
        PE_LOG_INFO("Converged at iteration {}", i);
        break;
      }
    }
  }

  auto total = std::chrono::steady_clock::now() - start;
  stats_.iterations = iteration_;
  stats_.final_exploitability = exploitability_;
  stats_.total_nodes = tree_->NodeCount();
  stats_.terminal_nodes = tree_->TerminalCount();
  stats_.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total).count();
  stats_.nash_distance = exploitability_;

  PE_LOG_INFO("Solve complete: {} iters, exploit={:.6f}, {} nodes, {:.0f}ms", iteration_,
              exploitability_, tree_->NodeCount(), stats_.elapsed_ms);
}

void PublicTreeSolver::TrainIteration(PublicTree& tree) {
  Range hero_range = Range::FullRange();
  Range villain_range = Range::FullRange();
  hero_range.Normalize();
  villain_range.Normalize();

  for (int player = 0; player < 2; ++player) {
    if (options_.use_chance_sampling) {
      CSTraverse(tree.Root(), hero_range, villain_range, player);
    } else {
      Traverse(tree.Root(), hero_range, villain_range, player, 1.0);
    }
  }
}

double PublicTreeSolver::Traverse(PublicTreeNode* node, const Range& hero_range,
                                  const Range& villain_range, int player, double reach_prob) {
  if (!node) return 0.0;

  if (node->is_terminal) {
    return TerminalValue(node, player, hero_range, villain_range);
  }

  CFRNode& strategy_node = node->strategy_node;
  strategy_node.compute_strategy();

  double* strategy = strategy_node.current_strategy;
  int num_actions = static_cast<int>(node->children.size());

  double node_utility = 0.0;
  std::vector<double> action_utils(num_actions, 0.0);

  for (int a = 0; a < num_actions; ++a) {
    auto& [action, child] = node->children[a];
    double action_prob = (a < CFRNode::kMaxActions) ? strategy[a] : 1.0 / num_actions;

    Range new_hero = hero_range;
    Range new_villain = villain_range;

    action_utils[a] = -Traverse(child, new_hero, new_villain, 1 - player, reach_prob * action_prob);
    node_utility += action_prob * action_utils[a];
  }

  for (int a = 0; a < num_actions; ++a) {
    double regret = action_utils[a] - node_utility;
    if (a < CFRNode::kMaxActions) {
      strategy_node.accumulate_regret(a, regret * reach_prob,
                                      options_.cfr_options.config.regret_floor,
                                      options_.cfr_options.config.regret_ceil);
      strategy_node.accumulate_strategy(a, strategy[a] * reach_prob);
    }
  }

  return node_utility;
}

double PublicTreeSolver::CSTraverse(PublicTreeNode* node, const Range& hero_range,
                                    const Range& villain_range, int player) {
  if (!node) return 0.0;

  if (node->is_terminal) {
    return TerminalValue(node, player, hero_range, villain_range);
  }

  CFRNode& strategy_node = node->strategy_node;
  strategy_node.compute_strategy();

  double* strategy = strategy_node.current_strategy;
  int num_actions = static_cast<int>(node->children.size());

  std::vector<double> action_utils(num_actions, 0.0);
  double node_utility = 0.0;

  for (int a = 0; a < num_actions; ++a) {
    auto& [action, child] = node->children[a];
    double action_prob = (a < CFRNode::kMaxActions) ? strategy[a] : 1.0 / num_actions;

    if (options_.use_chance_sampling && action_prob < options_.sampling_rate) {
      if (rng_() % 1000 < static_cast<int>(options_.sampling_rate * 1000)) {
        action_utils[a] = -CSTraverse(child, hero_range, villain_range, 1 - player);
      }
      continue;
    }

    action_utils[a] = -CSTraverse(child, hero_range, villain_range, 1 - player);
    node_utility += action_prob * action_utils[a];
  }

  for (int a = 0; a < num_actions; ++a) {
    double regret = action_utils[a] - node_utility;
    if (a < CFRNode::kMaxActions) {
      strategy_node.accumulate_regret(a, regret, options_.cfr_options.config.regret_floor,
                                      options_.cfr_options.config.regret_ceil);
      strategy_node.accumulate_strategy(a, strategy[a]);
    }
  }

  return node_utility;
}

double PublicTreeSolver::TerminalValue(PublicTreeNode* node, int player, const Range& hero_range,
                                       const Range& villain_range) const {
  if (!node || node->pot_size == 0) return 0.0;
  double pot = node->pot_size;

  double hero_weight = hero_range.TotalWeight();
  double villain_weight = villain_range.TotalWeight();
  double total = hero_weight + villain_weight;

  if (total <= 0) return 0.0;

  double hero_share = hero_weight / total;
  return (hero_share - 0.5) * pot;
}

double PublicTreeSolver::ComputeExploitability() {
  if (!tree_) return 0.0;

  double total_exploit = 0.0;

  Range hero = Range::FullRange();
  Range villain = Range::FullRange();
  hero.Normalize();
  villain.Normalize();

  for (int player = 0; player < 2; ++player) {
    total_exploit += std::abs(Traverse(tree_->Root(), hero, villain, player, 1.0));
  }

  return total_exploit / 2.0;
}

void PublicTreeSolver::GetStrategy(PublicTreeNode* node, uint16_t bucket,
                                   double out[kMaxActions]) const {
  if (!node) {
    for (int a = 0; a < kMaxActions; ++a) out[a] = 1.0 / kMaxActions;
    return;
  }

  const CFRNode& sn = node->strategy_node;
  double avg[CFRNode::kMaxActions];
  const_cast<CFRNode&>(sn).get_average_strategy(avg);

  for (int a = 0; a < kMaxActions; ++a) out[a] = avg[a];
}

void PublicTreeSolver::GetStrategyAll(PublicTreeNode* node,
                                      std::vector<std::vector<double>>& strategies) const {
  if (!node) return;

  double avg[CFRNode::kMaxActions];
  node->strategy_node.get_average_strategy(avg);

  for (uint16_t bucket : node->hand_buckets) {
    std::vector<double> strat(kMaxActions);
    for (int a = 0; a < kMaxActions; ++a) strat[a] = avg[a];
    strategies.push_back(strat);
  }

  for (auto& [action, child] : node->children) {
    GetStrategyAll(child, strategies);
  }
}

bool PublicTreeSolver::SaveSnapshot(const std::string& filepath) {
  if (!tree_) return false;

  std::unordered_map<uint64_t, CFRNode> nodes;
  std::queue<PublicTreeNode*> queue;
  queue.push(tree_->Root());

  while (!queue.empty()) {
    PublicTreeNode* n = queue.front();
    queue.pop();
    if (!n) continue;

    nodes[n->id] = n->strategy_node;
    for (auto& [action, child] : n->children) {
      queue.push(child);
    }
  }

  return CFRModelIO::Save(filepath, nodes, exploitability_);
}

bool PublicTreeSolver::LoadSnapshot(const std::string& filepath) {
  std::unordered_map<uint64_t, CFRNode> nodes;
  if (!CFRModelIO::Load(filepath, nodes)) return false;

  if (!tree_) return false;

  std::queue<PublicTreeNode*> queue;
  queue.push(tree_->Root());

  while (!queue.empty()) {
    PublicTreeNode* n = queue.front();
    queue.pop();
    if (!n) continue;

    auto it = nodes.find(n->id);
    if (it != nodes.end()) {
      n->strategy_node = it->second;
    }
    for (auto& [action, child] : n->children) {
      queue.push(child);
    }
  }

  PE_LOG_INFO("Loaded snapshot: {} nodes from {}", nodes.size(), filepath);
  return true;
}

void PublicTreeSolver::ApplyDiscount() {
  if (!tree_) return;

  double alpha = options_.cfr_options.config.alpha;
  double beta = options_.cfr_options.config.beta;
  double frac = static_cast<double>(iteration_) / 100.0;
  double regret_discount = std::pow(frac / (frac + 1), alpha);
  double strat_discount = std::pow(frac / (frac + 1), beta);

  std::queue<PublicTreeNode*> queue;
  queue.push(tree_->Root());

  while (!queue.empty()) {
    PublicTreeNode* n = queue.front();
    queue.pop();
    if (!n) continue;

    CFRNode& sn = n->strategy_node;
    for (int a = 0; a < CFRNode::kMaxActions; ++a) {
      if (sn.regret_sum[a] > 0) sn.regret_sum[a] *= regret_discount;
      if (options_.cfr_options.use_regret_matching_plus && sn.regret_sum[a] < 0) {
        sn.regret_sum[a] = 0;
      }
      sn.strategy_sum[a] *= strat_discount;
    }

    for (auto& [action, child] : n->children) {
      queue.push(child);
    }
  }
}

std::string PublicTreeSolver::SolveStats::ToString() const {
  return "SolveStats(iters=" + std::to_string(iterations) +
         ", exploit=" + std::to_string(final_exploitability) +
         ", nodes=" + std::to_string(total_nodes) + ", terminal=" + std::to_string(terminal_nodes) +
         ", ms=" + std::to_string(elapsed_ms) + ", nash=" + std::to_string(nash_distance) + ")";
}

}  // namespace poker_engine::cfr
