#pragma once

#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>

#include "cfr_abstraction.h"
#include "cfr_node.h"
#include "cfr_range.h"
#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/game/observation.h"
#include "types.h"

namespace poker_engine::cfr {

// Lightweight game state for CFR internal traversal.
// game::GameState has private members and doesn't support the direct
// field manipulation that CFR traversal requires, so we use this
// internal struct instead.
struct CFRGameState {
  bool hand_over = false;
  bool is_showdown = false;
  bool is_playing = true;
  double pot = 0.0;
  double current_bet = 0.0;
  int acting_player = 0;
  int dealer_seat = 0;
  std::vector<int32_t> winners;
};

struct CFROptions {
  CFRConfig config;

  int max_bet_level = 3;
  int pot_quantization = 32;

  bool enable_pruning = true;
  double prune_threshold = 0.0;

  bool use_regret_matching_plus = true;
  double exploration_epsilon = 0.0;

  double exploitability_threshold = 0.01;
  int check_interval = 100;
};

struct TraversalState {
  int acting_player;
  double reach_prob[2];
  double opponent_reach_prob;
  double sampling_prob;
  CFRGameState game_state;
  std::vector<int8_t> action_history;
};

class CFREngine {
 public:
  explicit CFREngine(const CFROptions& options = CFROptions());

  void Initialize();

  void Train(int num_iterations);
  void TrainIteration();

  double CFR(const TraversalState& state);
  double CS_CFR(const TraversalState& state);

  double ComputeExploitability();

  CFRNode* GetOrCreateNode(const InfosetKey& key);
  const CFRNode* GetNode(const InfosetKey& key) const;

  size_t NodeCount() const { return nodes_.size(); }
  const std::unordered_map<uint64_t, CFRNode>& Nodes() const { return nodes_; }

  std::vector<std::pair<Action, double>> GetStrategy(const InfosetKey& key);
  std::vector<std::pair<Action, double>> GetStrategyForState(const game::GameState& state,
                                                               int player) const;
  // Observation path: identical infoset key derivation as the GameState path
  // (shared ComputeInfosetKeyFromParts), so a CFR agent behaves the same whether
  // it sees the full GameState or a redacted Observation.
  std::vector<std::pair<Action, double>> GetStrategyForState(const game::Observation& obs,
                                                               int player) const;

  void PrintStats() const;

  const HandAbstraction& GetHandAbstraction() const { return hand_abstraction_; }
  const CFROptions& Options() const { return options_; }
  CFRConfig& Config() { return options_.config; }

  void SetPruning(bool enable) { options_.enable_pruning = enable; }

  void Reset();
  void ClearNodes();

  bool ImportNodes(const std::unordered_map<uint64_t, CFRNode>& nodes);

 private:
  InfosetKey ComputeInfosetKey(const TraversalState& state, int player) const;
  InfosetKey ComputeInfosetKey(const game::GameState& state, int player,
                               const cfr::HandAbstraction& abstraction) const;
  InfosetKey ComputeInfosetKey(const game::Observation& obs, int player) const;
  // Shared infoset-key derivation used by both the GameState and Observation
  // paths so their keys are byte-identical (no strategy drift).
  InfosetKey ComputeInfosetKeyFromParts(const game::HoleCards& own, game::GamePhase phase,
                                        double pot, double current_bet, uint8_t seat,
                                        const cfr::HandAbstraction& abstraction) const;
  InfosetKey ComputeInfosetKeyInternal(const CFRGameState& state, int player,
                                       const cfr::HandAbstraction& abstraction) const;

  std::vector<Action> GetLegalActions(const game::GameState& state, int player) const;

  void UpdateReachProbs(const TraversalState& old_state, int action_idx,
                        TraversalState& new_state) const;

  double TerminalValue(const CFRGameState& state, int player) const;

  CFROptions options_;
  evaluator::Evaluator evaluator_;
  cfr::HandAbstraction hand_abstraction_;

  std::unordered_map<uint64_t, CFRNode> nodes_;

  std::mt19937 rng_;

  int64_t nodes_touched_ = 0;
  int64_t terminal_reached_ = 0;

  std::uniform_int_distribution<uint8_t> card_dist_;
};

}  // namespace poker_engine::cfr
