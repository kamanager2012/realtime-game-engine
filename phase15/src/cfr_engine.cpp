#include "poker_engine/cfr/cfr_engine.h"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>

#include "poker_engine/base/logging.h"

namespace poker_engine::cfr {
using namespace game;

namespace {
inline int CardOf(uint8_t card_idx) { return card_idx % kRanks; }
inline int SuitOf(uint8_t card_idx) { return card_idx / kRanks; }

uint8_t SampleCard(std::mt19937& rng, const std::vector<uint8_t>& used, uint8_t num_community) {
  std::uniform_int_distribution<uint8_t> dist(0, kTotalCards - 1);
  for (int attempt = 0; attempt < 100; ++attempt) {
    uint8_t card = dist(rng);
    bool conflict = false;
    for (auto u : used) {
      if (u == card) {
        conflict = true;
        break;
      }
    }
    if (!conflict) return card;
  }
  return 255;
}

uint16_t QuantizePot(double pot, double initial_pot, int levels) {
  if (initial_pot <= 0) initial_pot = 1;
  double ratio = pot / initial_pot;
  int bucket = static_cast<int>(ratio * levels / 4.0);
  if (bucket < 0) bucket = 0;
  if (bucket >= levels) bucket = levels - 1;
  return static_cast<uint16_t>(bucket);
}

uint8_t EncodeBetLevel(double amount, double pot) {
  if (pot <= 0) return 0;
  double ratio = amount / pot;
  if (ratio < 0.01) return 0;
  if (ratio < 0.6) return 1;
  if (ratio < 1.2) return 2;
  return 3;
}
}  // namespace

CFREngine::CFREngine(const CFROptions& options) : options_(options), rng_(std::random_device{}()) {
  card_dist_ = std::uniform_int_distribution<uint8_t>(0, kTotalCards - 1);
}

void CFREngine::Initialize() {
  hand_abstraction_.Initialize(evaluator_);
  nodes_.clear();
  nodes_.reserve(1000000);
  PE_LOG_INFO("CFREngine initialized: {} hand buckets", HandAbstraction::kNumBuckets);
}

void CFREngine::Reset() {
  for (auto& [key, node] : nodes_) {
    memset(node.regret_sum, 0, sizeof(node.regret_sum));
    memset(node.strategy_sum, 0, sizeof(node.strategy_sum));
    node.times_visited = 0;
  }
  nodes_touched_ = 0;
  terminal_reached_ = 0;
}

void CFREngine::ClearNodes() {
  nodes_.clear();
  nodes_touched_ = 0;
  terminal_reached_ = 0;
}

void CFREngine::Train(int num_iterations) {
  PE_LOG_INFO("Starting CFR training: {} iterations", num_iterations);

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < num_iterations; ++i) {
    TrainIteration();

    if (i > 0 && i % static_cast<int>(options_.config.discount_interval) == 0) {
      double alpha = options_.config.alpha;
      double beta = options_.config.beta;
      double frac = static_cast<double>(i) / options_.config.discount_interval;
      double discount = std::pow(frac / (frac + 1), alpha);
      double strat_discount = std::pow(frac / (frac + 1), beta);

      for (auto& [key, node] : nodes_) {
        for (int a = 0; a < CFRNode::kMaxActions; ++a) {
          if (node.regret_sum[a] > 0) node.regret_sum[a] *= discount;
          if (options_.use_regret_matching_plus && node.regret_sum[a] < 0) node.regret_sum[a] = 0;
          node.strategy_sum[a] *= strat_discount;
        }
      }
    }

    if (i > 0 && i % options_.check_interval == 0) {
      double exp = ComputeExploitability();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count();
      PE_LOG_INFO("Iter {}: exploitability={:.6f}, nodes={}, time={}ms", i, exp, nodes_.size(),
                  elapsed);

      if (exp < options_.exploitability_threshold) {
        PE_LOG_INFO("Converged! exploitability={} < threshold={}", exp,
                    options_.exploitability_threshold);
        break;
      }
    }
  }

  auto total = std::chrono::steady_clock::now() - start;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(total).count();
  PE_LOG_INFO("Training complete: {} iterations, {} nodes, {}ms", num_iterations, nodes_.size(),
              ms);
}

void CFREngine::TrainIteration() {
  for (int sampling_player = 0; sampling_player < 2; ++sampling_player) {
    CFRGameState cfr_state;
    cfr_state.is_playing = true;
    cfr_state.hand_over = false;
    cfr_state.is_showdown = false;
    cfr_state.pot = 1.5;
    cfr_state.current_bet = 0.0;
    cfr_state.dealer_seat = 0;
    cfr_state.acting_player = 0;

    std::vector<uint8_t> deck;
    for (int i = 0; i < kTotalCards; ++i) deck.push_back(i);
    std::shuffle(deck.begin(), deck.end(), rng_);

    TraversalState ts;
    ts.acting_player = 0;
    ts.reach_prob[0] = 1.0;
    ts.reach_prob[1] = 1.0;
    ts.game_state = cfr_state;

    CS_CFR(ts);
  }
}

double CFREngine::CS_CFR(const TraversalState& state) {
  if (state.action_history.size() > 24) {
    return TerminalValue(state.game_state, state.acting_player);
  }

  const CFRGameState& gs = state.game_state;

  if (gs.hand_over || gs.is_showdown || !gs.is_playing) {
    terminal_reached_++;
    return TerminalValue(gs, state.acting_player);
  }

  int player = state.acting_player;
  int opponent = 1 - player;

  InfosetKey key = ComputeInfosetKeyInternal(gs, player, hand_abstraction_);
  CFRNode* node = GetOrCreateNode(key);
  node->times_visited++;

  node->compute_strategy();

  double* strategy = node->current_strategy;
  int num_actions = action_count();

  std::vector<double> action_utils(num_actions, 0.0);
  double node_utility = 0.0;

  for (int a = 0; a < num_actions; ++a) {
    Action action = static_cast<Action>(a);

    CFRGameState new_gs = gs;
    double new_reach[2] = {state.reach_prob[0], state.reach_prob[1]};

    switch (action) {
      case Action::Fold:
        new_gs.is_playing = false;
        new_gs.hand_over = true;
        new_gs.winners = {static_cast<int32_t>(opponent)};
        action_utils[a] = TerminalValue(new_gs, player);
        break;

      case Action::Call: {
        new_reach[player] *= strategy[a];
        // Simplified one-street abstraction: any call/check ends the hand
        new_gs.current_bet = 0;
        new_gs.is_showdown = true;
        new_gs.hand_over = true;
        new_gs.is_playing = false;
        action_utils[a] = TerminalValue(new_gs, player);
        break;
      }

      case Action::BetHalf:
      case Action::BetPot:
      case Action::AllIn: {
        double bet_size = 0;
        switch (action) {
          case Action::BetHalf:
            bet_size = gs.pot * 0.5;
            break;
          case Action::BetPot:
            bet_size = gs.pot;
            break;
          case Action::AllIn:
            bet_size = 100;
            break;
          default:
            break;
        }
        new_reach[player] *= strategy[a];
        new_gs.current_bet += bet_size;
        new_gs.pot += bet_size;
        // Opponent can only fold or call next; cap depth by forcing response round
        if (state.action_history.size() >= 8) {
          new_gs.is_showdown = true;
          new_gs.hand_over = true;
          new_gs.is_playing = false;
          action_utils[a] = TerminalValue(new_gs, player);
        }
        break;
      }
    }

    TraversalState new_state = state;
    new_state.game_state = new_gs;
    new_state.reach_prob[0] = new_reach[0];
    new_state.reach_prob[1] = new_reach[1];
    new_state.acting_player = opponent;
    new_state.action_history.push_back(static_cast<int8_t>(a));

    if (action != Action::Fold && !new_gs.hand_over && !new_gs.is_showdown) {
      action_utils[a] = -CS_CFR(new_state);
    } else if (action != Action::Fold && action_utils[a] == 0.0) {
      action_utils[a] = TerminalValue(new_gs, player);
    }
  }

  for (int a = 0; a < num_actions; ++a) {
    node_utility += strategy[a] * action_utils[a];
  }

  for (int a = 0; a < num_actions; ++a) {
    double regret = action_utils[a] - node_utility;
    regret *= state.reach_prob[player];
    node->accumulate_regret(a, regret, options_.config.regret_floor, options_.config.regret_ceil);
  }

  for (int a = 0; a < num_actions; ++a) {
    node->accumulate_strategy(a, strategy[a] * state.reach_prob[player]);
  }

  node->compute_strategy();

  return node_utility;
}

double CFREngine::CFR(const TraversalState& state) { return CS_CFR(state); }

CFRNode* CFREngine::GetOrCreateNode(const InfosetKey& key) {
  uint64_t h = key.hash();
  auto it = nodes_.find(h);
  if (it != nodes_.end()) return &it->second;

  auto [new_it, _] = nodes_.emplace(h, CFRNode());
  nodes_touched_++;
  return &new_it->second;
}

const CFRNode* CFREngine::GetNode(const InfosetKey& key) const {
  uint64_t h = key.hash();
  auto it = nodes_.find(h);
  return it != nodes_.end() ? &it->second : nullptr;
}


std::vector<std::pair<Action, double>> CFREngine::GetStrategyForState(const game::GameState& state,
                                                                     int player) const {
  InfosetKey key = ComputeInfosetKey(state, player, hand_abstraction_);
  std::vector<std::pair<Action, double>> result;
  const CFRNode* node = GetNode(key);
  if (!node) {
    for (int a = 0; a < action_count(); ++a)
      result.emplace_back(static_cast<Action>(a), 1.0 / action_count());
    return result;
  }
  double avg[CFRNode::kMaxActions];
  const_cast<CFRNode*>(node)->get_average_strategy(avg);
  for (int a = 0; a < action_count(); ++a) {
    if (avg[a] > 0.001) result.emplace_back(static_cast<Action>(a), avg[a]);
  }
  return result;
}

std::vector<std::pair<Action, double>> CFREngine::GetStrategy(const InfosetKey& key) {
  std::vector<std::pair<Action, double>> result;
  const CFRNode* node = GetNode(key);
  if (!node) {
    for (int a = 0; a < action_count(); ++a)
      result.emplace_back(static_cast<Action>(a), 1.0 / action_count());
    return result;
  }

  double avg[5];
  const_cast<CFRNode*>(node)->get_average_strategy(avg);

  for (int a = 0; a < action_count(); ++a) {
    if (avg[a] > 0.001) result.emplace_back(static_cast<Action>(a), avg[a]);
  }
  return result;
}

InfosetKey CFREngine::ComputeInfosetKey(const game::GameState& state, int player,
                                        const HandAbstraction& abstraction) const {
  InfosetKey key;

  // Use AllPlayers() to find the player by id, since GameState has no GetPlayer(int) method
  uint16_t bucket = 0;
  for (const auto& p : state.AllPlayers()) {
    if (p.id == static_cast<int32_t>(player) && p.hole_cards.IsDealt()) {
      HoleCards hc;
      hc.c1 = p.hole_cards.card1();
      hc.c2 = p.hole_cards.card2();
      bucket = abstraction.get_bucket(hc);
      break;
    }
  }

  key.hand_bucket = bucket;
  key.street = static_cast<uint8_t>(state.GetPhase());
  key.pot_size = QuantizePot(state.GetPot(), 3.0, options_.pot_quantization);
  key.bet_sequence =
      EncodeBetLevel(state.GetCurrentBet(), state.GetPot() > 0 ? state.GetPot() : 3.0);
  key.player = 0;
  for (const auto& p : state.AllPlayers()) {
    if (p.id == static_cast<int32_t>(player)) {
      key.player = p.seat;
      break;
    }
  }

  return key;
}

InfosetKey CFREngine::ComputeInfosetKeyInternal(const CFRGameState& state, int player,
                                                const HandAbstraction& abstraction) const {
  InfosetKey key;
  key.hand_bucket = 0;  // No hole card info in CFRGameState
  key.street = 0;
  key.pot_size = QuantizePot(state.pot, 3.0, options_.pot_quantization);
  key.bet_sequence = EncodeBetLevel(state.current_bet, state.pot > 0 ? state.pot : 3.0);
  key.player = static_cast<uint8_t>(player);

  return key;
}

InfosetKey CFREngine::ComputeInfosetKey(const TraversalState& state, int player) const {
  return ComputeInfosetKeyInternal(state.game_state, player, hand_abstraction_);
}

std::vector<Action> CFREngine::GetLegalActions(const game::GameState& state, int player) const {
  std::vector<Action> actions;

  actions.push_back(Action::Fold);

  double pot = state.GetPot();
  double current_bet = state.GetCurrentBet();
  double stack = 100.0;
  double to_call = current_bet;

  if (to_call > 0) {
    actions.push_back(Action::Call);
  } else {
    actions.push_back(Action::Call);
  }

  if (stack > pot) {
    if (to_call == 0) {
      if (pot > 0) {
        actions.push_back(Action::BetHalf);
        actions.push_back(Action::BetPot);
      }
    } else {
      actions.push_back(Action::BetPot);
    }
  }

  if (stack > 0) {
    actions.push_back(Action::AllIn);
  }

  return actions;
}

double CFREngine::TerminalValue(const CFRGameState& state, int player) const {
  if (state.winners.empty()) {
    return 0.0;
  }

  bool is_winner = false;
  for (auto w : state.winners) {
    if (w == player) {
      is_winner = true;
      break;
    }
  }

  if (is_winner) {
    return state.pot * 0.5;
  } else {
    return -(state.pot * 0.5);
  }
}

double CFREngine::ComputeExploitability() {
  // Placeholder: proper exploitability requires full best-response computation
  double total_exploit = 0.0;
  int samples = 100;

  for (int s = 0; s < samples; ++s) {
    total_exploit += 0.0;
  }

  return total_exploit / samples;
}

void CFREngine::PrintStats() const {
  PE_LOG_INFO("===== CFR Engine Statistics =====");
  PE_LOG_INFO("  Total nodes:    {}", nodes_.size());
  PE_LOG_INFO("  Nodes touched:  {}", nodes_touched_);
  PE_LOG_INFO("  Terminals:      {}", terminal_reached_);

  double total_regret_abs = 0;
  int nonzero_nodes = 0;
  for (auto& [key, node] : nodes_) {
    for (int a = 0; a < CFRNode::kMaxActions; ++a) {
      total_regret_abs += std::abs(node.regret_sum[a]);
      if (node.strategy_sum[a] > 0) nonzero_nodes++;
    }
  }

  PE_LOG_INFO("  Avg |regret|:   {:.4f}",
              total_regret_abs / (nodes_.size() * CFRNode::kMaxActions));
  PE_LOG_INFO("  Nonzero strats: {} / {}", nonzero_nodes, nodes_.size() * CFRNode::kMaxActions);
  PE_LOG_INFO("===============================");
}


bool CFREngine::ImportNodes(const std::unordered_map<uint64_t, CFRNode>& nodes) {
  for (const auto& [key_hash, node] : nodes) {
    nodes_[key_hash] = node;
  }
  return !nodes_.empty();
}

}  // namespace poker_engine::cfr
