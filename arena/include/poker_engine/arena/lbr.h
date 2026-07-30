#pragma once
#include <cstdint>

#include "poker_engine/game/game_state.h"
#include "poker_engine/network/ai_engine.h"

namespace poker_engine::arena {

// Local Best Response (LBR) exploitability estimate (Lisý & Bowling, 2017).
//
// LBR is a locally-greedy best-responder that plays N heads-up hands against a
// black-box opponent. Its measured win rate in mbb/100 (with a 95% CI) is a
// LOWER BOUND on the opponent's true full-game exploitability: larger means the
// opponent is more exploitable. The bound is valid for ANY legal LBR policy —
// belief modelling only affects tightness, not validity.
//
// When `bet` is enabled, LBR value-/bluff-bets on checked-to nodes (it may bet
// when it is not facing a bet, i.e. to_call == 0) and still only folds/calls
// when facing a bet. This is a strictly tighter — but still valid — lower bound
// than the fold/call-only form: betting lets LBR punish over-calling opponents
// (e.g. a CallStation) that a passive responder cannot exploit. With `bet`
// disabled it degrades to the fold/call-only variant.
//
// LBR obtains the opponent's per-hand behaviour by counterfactual probing: it
// asks a separate "probe" instance of the same agent what it would do with each
// hypothetical villain hole-card combo, then Bayes-filters its belief over the
// villain's range by consistency with the real action. Bet-sizing EV uses the
// same probe to estimate the villain's fold probability facing a candidate bet;
// EV accuracy only affects the bound's tightness, never its validity.
struct LbrConfig {
  int hands = 10000;
  uint64_t seed = 1;
  poker_engine::game::TableConfig table;
  // 0 => default of 200 big blinds; rebuilt before every hand (i.i.d. samples).
  poker_engine::game::Chips starting_stack = 0;
  // Allow LBR to bet on checked-to nodes (to_call == 0). false => fold/call only.
  bool bet = true;
};

struct LbrResult {
  double mbb_per_100 = 0.0;  // LBR win rate; a lower bound on exploitability
  double ci95 = 0.0;         // 95% confidence half-width on mbb_per_100
  long long hands_played = 0;
  bool chips_conserved = true;  // per-hand Σ net == 0 held for every hand

  // Sufficient statistics on LBR's per-hand mbb sample, so a multi-threaded
  // benchmark can pool shards and recompute mbb_per_100 / ci95 without loss.
  long long sample_n = 0;
  double sample_sum = 0.0;
  double sample_sumsq = 0.0;
};

// Play `config.hands` heads-up hands with LBR seated against `live_opponent`,
// using `probe_opponent` (a distinct instance of the SAME agent) purely for
// counterfactual belief probing so the live opponent's state/RNG is never
// disturbed. Returns LBR's mbb/100 lower bound on the opponent's exploitability.
// Deterministic for a fixed config.seed.
LbrResult RunLbr(poker_engine::network::IAIEngine& live_opponent,
                 poker_engine::network::IAIEngine& probe_opponent, const LbrConfig& config);

}  // namespace poker_engine::arena
