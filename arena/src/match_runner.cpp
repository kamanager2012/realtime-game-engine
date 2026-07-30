#include "poker_engine/arena/match_runner.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/game/action.h"
#include "poker_engine/game/player_state.h"
#include "poker_engine/range/hand_id.h"
#include "poker_engine/range/range.h"

namespace poker_engine::arena {

using poker_engine::game::ActionType;
using poker_engine::game::Chips;
using poker_engine::game::CommunityCards;
using poker_engine::game::GameAction;
using poker_engine::game::GameEvent;
using poker_engine::game::GameState;
using poker_engine::game::PlayerState;
using poker_engine::network::DecisionRequest;
using poker_engine::network::DecisionResponse;
using poker_engine::network::IAIEngine;

namespace {

Chips ChipsOf(const GameState& state, int32_t id) {
  for (const auto& p : state.AllPlayers()) {
    if (p.id == id) return p.chips;
  }
  return 0;
}

// Per-hand snapshot captured (via the game's EventCallback) at the moment a
// heads-up all-in triggers the community runout — BEFORE DoShowdown deals the
// remaining board and settles the pot. Records the pre-runout board and both
// all-in players' hole cards and total investment so the runner can replace the
// stochastic pot award with the exact equity-weighted expectation.
struct AllInLock {
  bool locked = false;
  CommunityCards prev_board;  // last normal-street board = pre-runout board
  CommunityCards pre_board;    // board frozen at the runout instant
  int32_t ids[2] = {0, 0};
  uint8_t c1[2] = {0, 0}, c2[2] = {0, 0};
  Chips inv[2] = {0, 0};
};

// Agent-0 heads-up equity of (c1a,c2a) vs (c1b,c2b) on `board`. Flop/turn (>=3
// cards) enumerate the remaining runout exactly; preflop uses a fixed-seed
// Monte Carlo (deterministic, internal seed 42) so results stay reproducible.
double HeadsUpEquity(uint8_t c1a, uint8_t c2a, uint8_t c1b, uint8_t c2b,
                     const CommunityCards& board) {
  poker_engine::range::Range ra, rb;
  ra.Set(poker_engine::range::HandId::Encode(c1a, c2a), 1.0f);
  rb.Set(poker_engine::range::HandId::Encode(c1b, c2b), 1.0f);
  uint8_t b[5] = {0, 0, 0, 0, 0};
  const int bs = board.count;
  for (int i = 0; i < bs; ++i) b[i] = board.cards[i];
  const int samples = bs >= 3 ? -1 : 20000;
  const auto eq = poker_engine::equity::EquityCalculator::CalculateExact(ra, rb, b, bs, samples);
  return static_cast<double>(eq.equity[0]);
}

// A safe legal action when an agent proposes something illegal: prefer the
// cheapest passive line (check > call > fold).
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

// Map an agent's proposal onto the legal action set, preserving intent
// (mirrors the live server's SanitizeBotAction semantics): CHECK↔CALL swap
// when facing / not facing a bet, BET↔RAISE conversion with the sizing intent
// clamped to [minimum, all-in], FOLD-when-free becomes CHECK, and aggressive
// intent degrades to ALL_IN when that is the only aggressive action left.
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
      if (check) return *check;  // never fold when checking is free
      return action;
    case ActionType::CHECK:
      return check ? *check : passive();
    case ActionType::CALL:
      return call ? *call : passive();
    case ActionType::BET:
    case ActionType::RAISE: {
      if (!min_agg) return all_in ? *all_in : passive();
      GameAction out = *min_agg;  // legal type + minimum legal size
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

// Play a single hand to completion. `agent_for` maps a seated player id to the
// controlling agent. Returns false if the hand could not start.
bool PlayOneHand(GameState& state, int k,
                 const std::function<IAIEngine&(int32_t)>& agent_for) {
  if (!state.StartHand()) return false;
  int guard = 0;
  while (state.IsHandInProgress() && guard++ < 100000) {
    int32_t cur = state.GetCurrentPlayerId();
    if (cur < 0) break;
    IAIEngine& agent = agent_for(cur);

    std::vector<GameAction> legal = state.LegalActions(cur);
    DecisionRequest req{state.ObserveFor(cur), cur, legal};
    DecisionResponse resp = agent.Decide(req);
    GameAction action = Sanitize(legal, state, cur, resp.action);
    action.player_id = cur;

    if (!state.ProcessAction(cur, action)) {
      GameAction fb = SafeFallback(legal, cur);
      fb.player_id = cur;
      if (!state.ProcessAction(cur, fb)) break;
    }
  }
  (void)k;
  return true;
}

}  // namespace

MatchResult RunMatch(const std::vector<IAIEngine*>& agents, const MatchConfig& config) {
  MatchResult result;
  const int k = static_cast<int>(agents.size());
  result.net_by_seat.assign(std::max(k, 0), 0);
  if (k < 2) return result;  // need at least two agents

  const Chips bb = config.table.big_blind;
  result.big_blind = static_cast<double>(bb);
  const Chips stack = config.starting_stack > 0 ? config.starting_stack : 200 * bb;
  const int reps = config.duplicate ? k : 1;
  const int H = config.hands;
  result.reps = reps;
  result.variance_reduced = config.duplicate;
  const bool aivat_on = config.aivat && k == 2;
  result.aivat_applied = aivat_on;

  // For duplicate mode we must pair agent 0's per-hand result across rotations,
  // so keep a per-rep array; independent mode only needs running moments.
  std::vector<std::vector<double>> a0_mbb;
  if (reps > 1) a0_mbb.assign(reps, std::vector<double>(H, 0.0));
  std::vector<int> hands_per_rep(reps, 0);

  double indep_sum = 0.0, indep_sumsq = 0.0;
  long long indep_n = 0;

  for (int r = 0; r < reps; ++r) {
    GameState state(config.table);
    state.SetDeterministicDeckSeed(config.seed);
    for (int s = 0; s < k; ++s) {
      state.AddPlayerAtSeat(s + 1, "P" + std::to_string(s), stack, static_cast<uint8_t>(s));
    }

    // Seat s is controlled by agents[(s + r) % k]; player id at seat s is s+1.
    auto agent_for = [&](int32_t player_id) -> IAIEngine& {
      const int seat = player_id - 1;
      return *agents[(seat + r) % k];
    };
    // Agent 0 sits seat s0 this rotation (the seat s with (s + r) % k == 0).
    const int seat_of_a0 = (k - (r % k)) % k;
    const int32_t id_of_a0 = seat_of_a0 + 1;

    // AIVAT: intercept the all-in community runout the instant it is dealt (the
    // engine runs the board out and settles synchronously inside ProcessAction,
    // so the runner loop cannot see the pre-runout board afterward).
    AllInLock lock;
    if (aivat_on) {
      state.SetCallback([&state, &lock](const GameEvent& ev) {
        if (ev.type != GameEvent::COMMUNITY_DEALT) return;
        int active_not_allin = 0, not_folded = 0;
        for (const auto& p : state.AllPlayers()) {
          if (p.IsActive()) {
            ++active_not_allin;
            ++not_folded;
          } else if (p.IsAllIn()) {
            ++not_folded;
          }
        }
        if (active_not_allin == 0 && not_folded == 2) {
          // Heads-up all-in runout: freeze the pre-runout board + both hands.
          lock.locked = true;
          lock.pre_board = lock.prev_board;
          int s = 0;
          for (const auto& p : state.AllPlayers()) {
            if (!p.IsActive() && !p.IsAllIn()) continue;
            if (s < 2) {
              lock.ids[s] = p.id;
              lock.c1[s] = p.hole_cards.card1();
              lock.c2[s] = p.hole_cards.card2();
              lock.inv[s] = p.bet_info.total_invested;
              ++s;
            }
          }
        } else {
          // Normal street deal: remember this board as the pre-runout snapshot.
          lock.prev_board = state.GetCommunity();
        }
      });
    }

    for (int h = 0; h < H; ++h) {
      if (aivat_on) lock = AllInLock{};
      if (!PlayOneHand(state, k, agent_for)) break;

      for (auto* ag : agents) ag->OnHandComplete(state);

      // Per-seat net; attribute to the controlling agent. Assert conservation.
      Chips hand_sum = 0;
      Chips a0_net = 0;
      for (int s = 0; s < k; ++s) {
        const int32_t id = s + 1;
        const Chips net = ChipsOf(state, id) - stack;
        hand_sum += net;
        result.net_by_seat[(s + r) % k] += net;
        if (id == id_of_a0) a0_net = net;
      }
      if (hand_sum != 0) result.chips_conserved = false;

      double x;
      if (aivat_on && lock.locked && lock.pre_board.count < 5) {
        const int a0slot = (lock.ids[0] == id_of_a0) ? 0 : 1;
        const int vslot = 1 - a0slot;
        const Chips m = std::min(lock.inv[a0slot], lock.inv[vslot]);
        const double e0 = HeadsUpEquity(lock.c1[a0slot], lock.c2[a0slot], lock.c1[vslot],
                                        lock.c2[vslot], lock.pre_board);
        const double ev_net_a0 = static_cast<double>(m) * (2.0 * e0 - 1.0);
        x = ev_net_a0 / static_cast<double>(bb) * 1000.0;  // mbb (EV-adjusted)
        ++result.adjusted_hands;
      } else {
        x = static_cast<double>(a0_net) / static_cast<double>(bb) * 1000.0;  // mbb
      }
      if (reps > 1) {
        a0_mbb[r][h] = x;
      } else {
        indep_sum += x;
        indep_sumsq += x * x;
        ++indep_n;
      }
      ++hands_per_rep[r];

      // In-place stack reset for the next hand → independent samples.
      for (int s = 0; s < k; ++s) state.SetPlayerChips(s + 1, stack);
    }
  }

  double sum = 0.0, sumsq = 0.0;
  long long n = 0;
  int total_hands = 0;
  for (int hp : hands_per_rep) total_hands += hp;

  if (reps > 1) {
    int hmin = hands_per_rep[0];
    for (int hp : hands_per_rep) hmin = std::min(hmin, hp);
    for (int h = 0; h < hmin; ++h) {
      double paired = 0.0;
      for (int r = 0; r < reps; ++r) paired += a0_mbb[r][h];
      const double x = paired / static_cast<double>(k);  // per-hand-equivalent mbb
      sum += x;
      sumsq += x * x;
    }
    n = hmin;
  } else {
    sum = indep_sum;
    sumsq = indep_sumsq;
    n = indep_n;
  }

  result.sample_n = n;
  result.sample_sum = sum;
  result.sample_sumsq = sumsq;
  result.hands_played = total_hands;
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

MatchResult RunHeadsUp(IAIEngine& a, IAIEngine& b, const MatchConfig& config) {
  std::vector<IAIEngine*> agents = {&a, &b};
  return RunMatch(agents, config);
}

}  // namespace poker_engine::arena
