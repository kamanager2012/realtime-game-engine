#include "poker_engine/arena/lbr.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/game/action.h"
#include "poker_engine/game/observation.h"
#include "poker_engine/game/player_state.h"
#include "poker_engine/range/hand_id.h"
#include "poker_engine/range/range.h"

namespace poker_engine::arena {

using poker_engine::game::ActionType;
using poker_engine::game::Chips;
using poker_engine::game::CommunityCards;
using poker_engine::game::GameAction;
using poker_engine::game::GameState;
using poker_engine::game::Observation;
using poker_engine::network::DecisionRequest;
using poker_engine::network::DecisionResponse;
using poker_engine::network::IAIEngine;
using poker_engine::range::HandId;
using poker_engine::range::Range;

namespace {

constexpr int32_t kLbrId = 1;       // LBR sits at seat 0
constexpr int32_t kVillainId = 2;   // opponent sits at seat 1

Chips ChipsOf(const GameState& state, int32_t id) {
  for (const auto& p : state.AllPlayers()) {
    if (p.id == id) return p.chips;
  }
  return 0;
}

// A safe passive legal action (check > call > fold) for coercing an illegal
// proposal onto the legal set.
GameAction SafeFallback(const std::vector<GameAction>& legal, int32_t id) {
  const GameAction* call = nullptr;
  for (const auto& a : legal) {
    if (a.type == ActionType::CHECK) return a;
    if (a.type == ActionType::CALL) call = &a;
  }
  if (call) return *call;
  GameAction fold;
  fold.type = ActionType::FOLD;
  fold.player_id = id;
  return fold;
}

// Map the opponent's proposal onto the legal action set, preserving intent
// (mirrors match_runner's Sanitize): CHECK↔CALL swap, BET↔RAISE sizing clamped
// to [minimum, all-in], FOLD-when-free becomes CHECK. Used only to DRIVE the
// engine with the live opponent's action; belief filtering compares the raw
// (unsanitized) action types so probe and live stay on equal footing.
GameAction Sanitize(const std::vector<GameAction>& legal, const GameState& state, int32_t id,
                    GameAction action) {
  const GameAction* check = nullptr;
  const GameAction* call = nullptr;
  const GameAction* min_agg = nullptr;
  const GameAction* all_in = nullptr;
  for (const auto& a : legal) {
    switch (a.type) {
      case ActionType::CHECK: check = &a; break;
      case ActionType::CALL: call = &a; break;
      case ActionType::BET:
      case ActionType::RAISE: min_agg = &a; break;
      case ActionType::ALL_IN: all_in = &a; break;
      default: break;
    }
  }
  auto passive = [&]() { return check ? *check : SafeFallback(legal, id); };

  switch (action.type) {
    case ActionType::FOLD:
      if (check) return *check;
      return action;
    case ActionType::CHECK:
      return check ? *check : passive();
    case ActionType::CALL:
      return call ? *call : passive();
    case ActionType::BET:
    case ActionType::RAISE: {
      if (!min_agg) return all_in ? *all_in : passive();
      GameAction out = *min_agg;
      Chips my_chips = 0;
      Chips my_cur = 0;
      for (const auto& p : state.AllPlayers()) {
        if (p.id == id) {
          my_chips = p.chips;
          my_cur = p.bet_info.current_bet;
          break;
        }
      }
      const Chips max_total = my_chips + my_cur;
      if (action.amount > out.amount) out.amount = std::min(action.amount, max_total);
      return out;
    }
    case ActionType::ALL_IN:
      return all_in ? *all_in : passive();
    default:
      return passive();
  }
}

// Uniform belief over the villain's 1326 hole-card combos with LBR's own cards
// and the current board removed, then normalized. Used at hand start and as the
// reset target if a Bayesian update empties the belief.
Range MakeBaseBelief(uint8_t lbr_c1, uint8_t lbr_c2, const CommunityCards& board) {
  Range r = Range::Uniform();
  r.RemoveCard(lbr_c1);
  r.RemoveCard(lbr_c2);
  for (int i = 0; i < board.count; ++i) r.RemoveCard(board.cards[i]);
  r.Normalize();
  return r;
}

// LBR's equity: its exact hand vs the weighted belief range on the current
// board. Flop+ enumerates the runout exactly; preflop uses fixed-seed MC
// (deterministic). Returns 0.5 when the belief carries no usable mass.
double BeliefEquity(const Observation& obs, const Range& belief) {
  const uint8_t c1 = obs.MyHoleCards().card1();
  const uint8_t c2 = obs.MyHoleCards().card2();
  Range lbr;
  lbr.Set(HandId::Encode(c1, c2), 1.0f);

  Range villain = belief;
  villain.RemoveCard(c1);
  villain.RemoveCard(c2);
  uint8_t b[5] = {0, 0, 0, 0, 0};
  const int bs = obs.community.count;
  for (int i = 0; i < bs; ++i) {
    b[i] = obs.community.cards[i];
    villain.RemoveCard(b[i]);
  }
  if (villain.Sum() <= 1e-8f) return 0.5;

  // Flop+ enumerates exactly; preflop uses a modest fixed-seed MC. Equity
  // accuracy only affects the tightness of the bound, not its validity, so a
  // lighter preflop sample keeps LBR fast while staying deterministic.
  const int samples = bs >= 3 ? -1 : 4000;
  const auto eq =
      poker_engine::equity::EquityCalculator::CalculateExact(lbr, villain, b, bs, samples);
  return static_cast<double>(eq.equity[0]);
}

// Derive the villain's observation as if LBR had just bet up to `new_total` this
// street (additional wager `g`). Public fields shift accordingly: the current
// bet rises to new_total, the pot grows by g, and LBR's own PlayerView reflects
// the committed chips. Used only for counterfactual fold probing; validity of
// the LBR bound does not depend on this synthesis being exact.
Observation SynthVillainFacingBet(const Observation& v_now, Chips new_total, Chips g) {
  Observation o = v_now;
  o.current_bet = new_total;
  o.pot = v_now.pot + g;
  for (auto& p : o.players) {
    if (p.id == kLbrId) {
      p.bet_info.current_bet = new_total;
      p.chips = p.chips > g ? p.chips - g : 0;
      p.acted_this_round = true;
      break;
    }
  }
  return o;
}

// Synthesize the villain's legal action set facing LBR's bet to `new_total`.
// Agents key their fold/continue decision primarily off the action types on
// offer plus the pot-odds implied by current_bet/pot, both of which are set on
// the synthesized observation, so approximate raise sizing is harmless.
std::vector<GameAction> SynthLegalFacingBet(const Observation& v_now, Chips new_total) {
  std::vector<GameAction> legal;
  const int32_t vid = v_now.viewer_id;
  const Chips vmax_total = v_now.MyCurrentBet() + v_now.MyChips();
  GameAction fold;
  fold.type = ActionType::FOLD;
  fold.player_id = vid;
  legal.push_back(fold);
  GameAction call;
  call.type = ActionType::CALL;
  call.player_id = vid;
  call.amount = std::min(new_total, vmax_total);
  legal.push_back(call);
  if (vmax_total > new_total) {  // villain has chips to raise
    GameAction raise;
    raise.type = ActionType::RAISE;
    raise.player_id = vid;
    raise.amount = std::min(new_total * 2, vmax_total);
    legal.push_back(raise);
    GameAction allin;
    allin.type = ActionType::ALL_IN;
    allin.player_id = vid;
    allin.amount = vmax_total;
    legal.push_back(allin);
  }
  return legal;
}

// Counterfactually probe the villain's fold probability facing a synthesized
// bet: for each belief combo, ask the probe what it would do, accumulate FOLD
// mass, and collect the non-folding combos into `call_range` (normalized).
// Returns the fold fraction; falls back to the prior belief if the range is
// empty. Only affects bet-EV tightness, never the bound's validity.
double FoldProbeCallRange(const Range& belief, const Observation& v_facing,
                          const std::vector<GameAction>& legal_facing, IAIEngine& probe,
                          Range* call_range) {
  Range calls;
  double fold_mass = 0.0;
  double total = 0.0;
  const auto& data = belief.Data();
  for (int id = 0; id < HandId::TOTAL_COMBOS; ++id) {
    const float w = data[id];
    if (w <= 1e-8f) continue;
    total += w;
    auto [lo, hi] = HandId::Decode(id);
    Observation po = v_facing;
    po.my_hole_cards.Set(lo, hi);
    DecisionRequest req{po, v_facing.viewer_id, legal_facing};
    const DecisionResponse resp = probe.Decide(req);
    if (resp.action.type == ActionType::FOLD) {
      fold_mass += w;
    } else {
      calls.Set(static_cast<uint16_t>(id), w);
    }
  }
  if (total <= 1e-8) {
    *call_range = belief;
    return 0.0;
  }
  calls.Normalize();
  *call_range = calls;
  return fold_mass / total;
}

// LBR's own decision. Facing a bet (to_call > 0) it only folds/calls by pot
// odds (never raises). On a checked-to node (to_call == 0) it may bet: it
// compares checking (util = wp0*pot) against candidate bet sizes, whose EV uses
// the counterfactually probed villain fold probability and LBR's equity vs the
// non-folding range. It bets the argmax only when it strictly beats checking,
// so LBR value-/bluff-bets to punish over-calling opponents. Betting is skipped
// when `allow_bet` is false (degrading to the fold/call-only variant).
GameAction LbrDecide(const Observation& obs, const Observation& villain_obs,
                     const std::vector<GameAction>& legal, const Range& belief, bool allow_bet,
                     IAIEngine& probe) {
  const GameAction* check = nullptr;
  const GameAction* call = nullptr;
  const GameAction* fold = nullptr;
  const GameAction* min_agg = nullptr;
  for (const auto& a : legal) {
    if (a.type == ActionType::CHECK) check = &a;
    else if (a.type == ActionType::CALL) call = &a;
    else if (a.type == ActionType::FOLD) fold = &a;
    else if (a.type == ActionType::BET || a.type == ActionType::RAISE) min_agg = &a;
  }

  const Chips to_call = obs.current_bet - obs.MyCurrentBet();
  if (to_call > 0) {
    const double pot = static_cast<double>(obs.pot);
    const double tc = static_cast<double>(to_call);
    const double eq = BeliefEquity(obs, belief);
    const double threshold = tc / (pot + tc);
    if (eq >= threshold && call) return *call;
    if (fold) return *fold;
    if (call) return *call;
    if (check) return *check;
    GameAction f;
    f.type = ActionType::FOLD;
    f.player_id = obs.viewer_id;
    return f;
  }

  // to_call == 0: free check available. Consider betting if allowed.
  if (allow_bet && min_agg) {
    const double pot0 = static_cast<double>(obs.pot);
    const double wp0 = BeliefEquity(obs, belief);
    double best_util = wp0 * pot0;  // utility of checking
    const GameAction* best = check;

    const Chips my_cur = obs.MyCurrentBet();
    const Chips min_total = min_agg->amount;
    // The engine's ALL_IN legal action carries amount==0 (size computed
    // internally), so derive LBR's all-in total from its own stack.
    const Chips allin_total = my_cur + obs.MyChips();
    const Chips pot_total = std::min(std::max(my_cur + static_cast<Chips>(obs.pot), min_total),
                                     allin_total);
    Chips cands[2] = {pot_total, allin_total};
    const Chips villain_chips = villain_obs.MyChips();

    GameAction chosen;
    bool have_bet = false;
    for (int i = 0; i < 2; ++i) {
      const Chips cand = cands[i];
      if (cand < min_total || cand > allin_total) continue;
      if (i == 1 && cands[1] == cands[0]) continue;  // dedup all-in == pot
      const Chips g = cand - my_cur;
      if (g <= 0) continue;

      const Observation v_facing = SynthVillainFacingBet(villain_obs, cand, g);
      const std::vector<GameAction> legal_facing = SynthLegalFacingBet(villain_obs, cand);
      Range call_range;
      const double fp = FoldProbeCallRange(belief, v_facing, legal_facing, probe, &call_range);
      // When the villain folds ~nothing (e.g. a CallStation), its calling range
      // is the full belief, so wp1 == wp0 and the second equity is redundant.
      const double wp1 = fp <= 1e-6 ? wp0
                         : call_range.Sum() > 1e-8f ? BeliefEquity(obs, call_range)
                                                    : wp0;

      const double m = static_cast<double>(std::min(g, villain_chips));  // matched if called
      const double util_bet =
          fp * pot0 + (1.0 - fp) * (wp1 * (pot0 + m) - (1.0 - wp1) * m);
      if (util_bet > best_util) {
        best_util = util_bet;
        chosen = *min_agg;
        chosen.amount = cand;
        have_bet = true;
      }
    }
    if (have_bet) return chosen;
    if (best) return *best;
  }

  if (check) return *check;
  if (call) return *call;
  GameAction f;
  f.type = ActionType::FOLD;
  f.player_id = obs.viewer_id;
  return f;
}

// Bayes-filter the belief by the villain's real action: keep each combo whose
// counterfactually probed action type matches, then renormalize. If the update
// would empty the belief (e.g. a mixed-strategy sample disagreed), keep the
// prior belief unchanged — a conservative guard that never invalidates the LBR
// lower bound (validity is independent of belief accuracy).
void BeliefFilter(Range& belief, const Observation& villain_obs,
                  const std::vector<GameAction>& legal, ActionType real_type, IAIEngine& probe) {
  Range filtered;
  bool any = false;
  const auto& data = belief.Data();
  for (int id = 0; id < HandId::TOTAL_COMBOS; ++id) {
    const float w = data[id];
    if (w <= 1e-8f) continue;
    auto [lo, hi] = HandId::Decode(id);
    Observation probe_obs = villain_obs;
    probe_obs.my_hole_cards.Set(lo, hi);
    DecisionRequest req{probe_obs, villain_obs.viewer_id, legal};
    const DecisionResponse resp = probe.Decide(req);
    if (resp.action.type == real_type) {
      filtered.Set(static_cast<uint16_t>(id), w);
      any = true;
    }
  }
  if (any) {
    belief = filtered;
    belief.Normalize();
  }
}

}  // namespace

LbrResult RunLbr(IAIEngine& live_opponent, IAIEngine& probe_opponent, const LbrConfig& config) {
  LbrResult result;
  const Chips bb = config.table.big_blind;
  if (bb <= 0) return result;
  const Chips stack = config.starting_stack > 0 ? config.starting_stack : 200 * bb;

  GameState state(config.table);
  state.SetDeterministicDeckSeed(config.seed);
  state.AddPlayerAtSeat(kLbrId, "LBR", stack, 0);
  state.AddPlayerAtSeat(kVillainId, "Villain", stack, 1);

  double sum = 0.0, sumsq = 0.0;
  long long n = 0;
  int hands_done = 0;

  for (int h = 0; h < config.hands; ++h) {
    if (!state.StartHand()) break;

    Range belief;
    bool belief_ready = false;
    int last_board_count = 0;

    int guard = 0;
    while (state.IsHandInProgress() && guard++ < 100000) {
      const int32_t cur = state.GetCurrentPlayerId();
      if (cur < 0) break;

      const CommunityCards board = state.GetCommunity();
      if (belief_ready && board.count > last_board_count) {
        for (int i = last_board_count; i < board.count; ++i) belief.RemoveCard(board.cards[i]);
        belief.Normalize();
        last_board_count = board.count;
      }

      const std::vector<GameAction> legal = state.LegalActions(cur);

      if (cur == kLbrId) {
        const Observation obs = state.ObserveFor(kLbrId);
        if (!belief_ready) {
          belief = MakeBaseBelief(obs.MyHoleCards().card1(), obs.MyHoleCards().card2(), board);
          belief_ready = true;
          last_board_count = board.count;
        }
        const Observation villain_obs_now = state.ObserveFor(kVillainId);
        GameAction action = LbrDecide(obs, villain_obs_now, legal, belief, config.bet, probe_opponent);
        action.player_id = cur;
        if (!state.ProcessAction(cur, action)) {
          GameAction fb = SafeFallback(legal, cur);
          fb.player_id = cur;
          if (!state.ProcessAction(cur, fb)) break;
        }
      } else {
        const Observation villain_obs = state.ObserveFor(kVillainId);
        const DecisionResponse resp = live_opponent.Decide({villain_obs, kVillainId, legal});
        if (belief_ready) {
          BeliefFilter(belief, villain_obs, legal, resp.action.type, probe_opponent);
        }
        GameAction action = Sanitize(legal, state, cur, resp.action);
        action.player_id = cur;
        if (!state.ProcessAction(cur, action)) {
          GameAction fb = SafeFallback(legal, cur);
          fb.player_id = cur;
          if (!state.ProcessAction(cur, fb)) break;
        }
      }
    }

    live_opponent.OnHandComplete(state);
    probe_opponent.OnHandComplete(state);

    const Chips net_lbr = ChipsOf(state, kLbrId) - stack;
    const Chips net_v = ChipsOf(state, kVillainId) - stack;
    if (net_lbr + net_v != 0) result.chips_conserved = false;

    const double x = static_cast<double>(net_lbr) / static_cast<double>(bb) * 1000.0;  // mbb
    sum += x;
    sumsq += x * x;
    ++n;
    ++hands_done;

    state.SetPlayerChips(kLbrId, stack);
    state.SetPlayerChips(kVillainId, stack);
  }

  result.sample_n = n;
  result.sample_sum = sum;
  result.sample_sumsq = sumsq;
  result.hands_played = hands_done;
  if (n > 0) {
    const double mean = sum / static_cast<double>(n);
    result.mbb_per_100 = mean * 100.0;
    if (n > 1) {
      const double var = (sumsq - static_cast<double>(n) * mean * mean) / static_cast<double>(n - 1);
      const double se = std::sqrt(var / static_cast<double>(n));
      result.ci95 = 1.96 * se * 100.0;
    }
  }
  return result;
}

}  // namespace poker_engine::arena
