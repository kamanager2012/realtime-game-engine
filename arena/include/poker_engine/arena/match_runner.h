#pragma once
#include <cstdint>
#include <vector>

#include "poker_engine/game/game_state.h"
#include "poker_engine/network/ai_engine.h"

namespace poker_engine::arena {

// Configuration for a headless bot-vs-bot match.
struct MatchConfig {
  int hands = 10000;
  uint64_t seed = 1;
  // Caller sets blinds/ante here; agents are seated at seats 0..k-1.
  poker_engine::game::TableConfig table;
  // Starting stack rebuilt before every hand so results are i.i.d. samples.
  // 0 => default of 200 big blinds (deep enough to avoid artificial all-ins).
  poker_engine::game::Chips starting_stack = 0;
  // Variance reduction via seat rotation ("duplicate" poker): the same
  // per-hand deck is played `k = agents.size()` times, rotating which agent
  // occupies which seat, so every agent plays every seat on identical cards.
  // The deal luck cancels in agent 0's cross-rotation sum, shrinking the CI.
  bool duplicate = false;
};

// Result of a match. All chip figures are in cents (Chips).
struct MatchResult {
  // Net chips won across the match, indexed by AGENT (0 = agent A, ...),
  // summed over all rotations.
  std::vector<int64_t> net_by_seat;
  double mbb_per_100 = 0.0;  // agent 0 win rate in milli-big-blinds per 100 hands
  double ci95 = 0.0;         // 95% confidence half-width on mbb_per_100
  int hands_played = 0;      // total hands dealt across all rotations
  bool chips_conserved = true;  // per-hand Σ net == 0 held for every hand
  double big_blind = 0.0;       // in chips (cents), for reporting
  int reps = 1;                 // 1 for independent, = num agents for duplicate
  bool variance_reduced = false;

  // Sufficient statistics on agent 0's per-hand-equivalent mbb sample, exposed
  // so callers (e.g. a multi-threaded benchmark) can pool shards and recompute
  // mbb_per_100 / ci95 without losing precision.
  long long sample_n = 0;
  double sample_sum = 0.0;
  double sample_sumsq = 0.0;
};

// Run `config.hands` hands among `agents` (2..table.max_players) on the real
// game engine (ProcessAction is the source of truth). The button alternates
// every hand so positional advantage cancels. Each hand starts from a fresh
// fixed stack, yielding independent per-hand samples used to estimate agent 0's
// mbb/100 win rate with a 95% confidence interval. Illegal agent proposals fall
// back to check/call/fold. Deterministic for a fixed config.seed.
MatchResult RunMatch(const std::vector<poker_engine::network::IAIEngine*>& agents,
                     const MatchConfig& config);

// Heads-up convenience wrapper: agent `a` is agent 0, `b` is agent 1.
MatchResult RunHeadsUp(poker_engine::network::IAIEngine& a,
                       poker_engine::network::IAIEngine& b, const MatchConfig& config);

}  // namespace poker_engine::arena
