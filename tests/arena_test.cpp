// Arena (agent research seam) tests: legality, chip conservation,
// determinism, and baseline separation.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "poker_engine/arena/baseline_agents.h"
#include "poker_engine/arena/lbr.h"
#include "poker_engine/arena/match_runner.h"
#include "poker_engine/arena/random_agent.h"
#include "poker_engine/game/action_validator.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/network/ai_engine.h"

namespace {

using poker_engine::arena::CallStationAgent;
using poker_engine::arena::LbrConfig;
using poker_engine::arena::LbrResult;
using poker_engine::arena::ManiacAgent;
using poker_engine::arena::MatchConfig;
using poker_engine::arena::MatchResult;
using poker_engine::arena::RandomAgent;
using poker_engine::arena::RunHeadsUp;
using poker_engine::arena::RunLbr;
using poker_engine::arena::RunMatch;
using poker_engine::game::ActionType;
using poker_engine::game::ActionValidator;
using poker_engine::game::Chips;
using poker_engine::game::GameAction;
using poker_engine::game::GameState;
using poker_engine::game::PlayerState;
using poker_engine::game::TableConfig;
using poker_engine::network::AIConfig;
using poker_engine::network::AIStrategyType;
using poker_engine::network::CreateAIEngine;
using poker_engine::network::IAIEngine;

TableConfig SmallTable() {
  TableConfig t;
  t.max_players = 6;
  t.small_blind = 50;
  t.big_blind = 100;
  t.ante = 0;
  t.min_buy_in = 1000;
  t.max_buy_in = 1000000;
  return t;
}

MatchConfig BenchConfig(int hands, uint64_t seed) {
  MatchConfig cfg;
  cfg.hands = hands;
  cfg.seed = seed;
  cfg.table = SmallTable();
  return cfg;
}

// Every GameAction returned by GameState::LegalActions must pass the
// validator; this is the contract agents rely on.
TEST(ArenaTest, LegalActionsAreValidatorLegal) {
  TableConfig t = SmallTable();
  GameState state(t);
  state.SetDeterministicDeckSeed(7);
  state.AddPlayerAtSeat(1, "A", 20000, 0);
  state.AddPlayerAtSeat(2, "B", 20000, 1);
  ASSERT_TRUE(state.StartHand());

  RandomAgent agent(AIConfig{});
  int checked = 0;
  int guard = 0;
  while (state.IsHandInProgress() && guard++ < 10000) {
    int32_t cur = state.GetCurrentPlayerId();
    if (cur < 0) break;

    std::vector<GameAction> legal = state.LegalActions(cur);
    ASSERT_FALSE(legal.empty());

    const PlayerState* me = nullptr;
    std::vector<PlayerState*> active;
    for (const auto& p : state.AllPlayers()) {
      if (p.id == cur) me = &p;
      if (p.IsActive() || p.IsAllIn()) active.push_back(const_cast<PlayerState*>(&p));
    }
    ASSERT_NE(me, nullptr);
    int all_in = 0;
    for (const auto* p : active) {
      if (p->IsAllIn()) all_in++;
    }

    for (const auto& a : legal) {
      auto v = ActionValidator::Validate(a, *me, active, state.GetCurrentBet(), state.GetPot(),
                                         t.big_blind, t.ante, state.ActivePlayerCount(), all_in,
                                         0, 0);
      // last_raise passed as 0 here is the loosest floor; LegalActions uses
      // the true (>=) value internally, so legality must still hold.
      EXPECT_TRUE(v.valid) << a.ToString() << " rejected: " << v.error;
      checked++;
    }

    poker_engine::network::DecisionRequest req{state.ObserveFor(cur), cur, legal};
    GameAction act = agent.Decide(req).action;
    act.player_id = cur;
    ASSERT_TRUE(state.ProcessAction(cur, act)) << act.ToString();
  }
  EXPECT_GT(checked, 0);
}

// Redaction contract: ObserveFor exposes the viewer's own hole cards but never
// an opponent's. Opponents appear as PlayerView, which has no hole_cards field
// at all (structural guarantee), yet still reports has_cards for public "was
// dealt in" info.
TEST(ArenaTest, ObservationHidesOpponentHoleCards) {
  TableConfig t = SmallTable();
  GameState state(t);
  state.SetDeterministicDeckSeed(7);
  state.AddPlayerAtSeat(1, "A", 20000, 0);
  state.AddPlayerAtSeat(2, "B", 20000, 1);
  ASSERT_TRUE(state.StartHand());

  auto obs = state.ObserveFor(1);
  EXPECT_EQ(obs.viewer_id, 1);
  EXPECT_TRUE(obs.MyHoleCards().IsDealt());

  const auto* me = obs.Me();
  ASSERT_NE(me, nullptr);
  EXPECT_TRUE(me->has_cards);

  // The opponent seat reports it was dealt cards, but there is no way to read
  // which cards (no field exists). Empty seats are skipped.
  bool saw_opponent = false;
  for (const auto& pv : obs.players) {
    if (pv.id != 2) continue;
    saw_opponent = true;
    EXPECT_TRUE(pv.has_cards);
  }
  EXPECT_TRUE(saw_opponent);
}

// Regression guard bound to the pot-conservation fix: over many random hands
// the two seats' net chips must sum to exactly zero, every hand.
TEST(ArenaTest, HeadsUpMatchConservesChips) {
  RandomAgent a{AIConfig{}};
  AIConfig cb;
  cb.random_seed = 99;
  RandomAgent b{cb};

  MatchResult r = RunHeadsUp(a, b, BenchConfig(500, 123));
  EXPECT_EQ(r.hands_played, 500);
  EXPECT_TRUE(r.chips_conserved);
  EXPECT_EQ(r.net_by_seat[0] + r.net_by_seat[1], 0);
}

// Reproducibility: identical seeds must reproduce the exact match outcome.
TEST(ArenaTest, MatchIsDeterministicForSeed) {
  auto run = []() {
    AIConfig ca;
    ca.random_seed = 5;
    AIConfig cb;
    cb.random_seed = 6;
    RandomAgent a{ca};
    RandomAgent b{cb};
    return RunHeadsUp(a, b, BenchConfig(200, 77));
  };
  MatchResult r1 = run();
  MatchResult r2 = run();
  EXPECT_EQ(r1.net_by_seat, r2.net_by_seat);
  EXPECT_EQ(r1.hands_played, r2.hands_played);
  EXPECT_DOUBLE_EQ(r1.mbb_per_100, r2.mbb_per_100);
}

// The benchmark must separate a strategic baseline from the random floor:
// rule-based should beat random with statistical significance.
TEST(ArenaTest, RuleBeatsRandomWithSignal) {
  AIConfig rule_cfg;
  rule_cfg.strategy = AIStrategyType::RuleBased;
  rule_cfg.random_seed = 11;
  auto rule = CreateAIEngine(rule_cfg);

  AIConfig rand_cfg;
  rand_cfg.random_seed = 22;
  RandomAgent random{rand_cfg};

  MatchResult r = RunHeadsUp(*rule, random, BenchConfig(2000, 2024));
  EXPECT_TRUE(r.chips_conserved);
  // Significance: the lower CI bound of rule's win rate stays above zero.
  EXPECT_GT(r.mbb_per_100 - r.ci95, 0.0)
      << "mbb/100=" << r.mbb_per_100 << " ci95=" << r.ci95;
}

// Duplicate (seat-rotation) pairing must shrink the confidence interval for the
// same matchup and hand budget by cancelling deal luck, while still conserving.
TEST(ArenaTest, DuplicateReducesVariance) {
  auto make_rule = []() {
    AIConfig c;
    c.strategy = AIStrategyType::RuleBased;
    c.random_seed = 11;
    return CreateAIEngine(c);
  };

  auto rule_i = make_rule();
  AIConfig rand_cfg;
  rand_cfg.random_seed = 22;
  RandomAgent rand_i{rand_cfg};
  MatchConfig indep = BenchConfig(1500, 2024);
  MatchResult ri = RunHeadsUp(*rule_i, rand_i, indep);

  auto rule_d = make_rule();
  RandomAgent rand_d{rand_cfg};
  MatchConfig dup = BenchConfig(1500, 2024);
  dup.duplicate = true;
  MatchResult rd = RunHeadsUp(*rule_d, rand_d, dup);

  EXPECT_TRUE(ri.chips_conserved);
  EXPECT_TRUE(rd.chips_conserved);
  EXPECT_TRUE(rd.variance_reduced);
  EXPECT_EQ(rd.reps, 2);
  EXPECT_GT(ri.ci95, 0.0);
  EXPECT_LT(rd.ci95, ri.ci95) << "duplicate ci=" << rd.ci95 << " independent ci=" << ri.ci95;
}

// N-way (>2) matches must conserve chips every hand.
TEST(ArenaTest, ThreeWayMatchConservesChips) {
  AIConfig c0, c1, c2;
  c0.random_seed = 1;
  c1.random_seed = 2;
  c2.random_seed = 3;
  RandomAgent a0{c0}, a1{c1}, a2{c2};
  std::vector<poker_engine::network::IAIEngine*> agents = {&a0, &a1, &a2};

  MatchResult r = RunMatch(agents, BenchConfig(400, 55));
  EXPECT_EQ(r.hands_played, 400);
  EXPECT_TRUE(r.chips_conserved);
  ASSERT_EQ(r.net_by_seat.size(), 3u);
  EXPECT_EQ(r.net_by_seat[0] + r.net_by_seat[1] + r.net_by_seat[2], 0);
}

// Duplicate mode must remain reproducible for a fixed seed.
TEST(ArenaTest, DuplicateIsDeterministic) {
  auto run = []() {
    AIConfig ca;
    ca.random_seed = 5;
    AIConfig cb;
    cb.random_seed = 6;
    RandomAgent a{ca};
    RandomAgent b{cb};
    MatchConfig cfg = BenchConfig(150, 77);
    cfg.duplicate = true;
    return RunHeadsUp(a, b, cfg);
  };
  MatchResult r1 = run();
  MatchResult r2 = run();
  EXPECT_EQ(r1.net_by_seat, r2.net_by_seat);
  EXPECT_EQ(r1.hands_played, r2.hands_played);
  EXPECT_DOUBLE_EQ(r1.mbb_per_100, r2.mbb_per_100);
}

// All-in EV adjustment (AIVAT-lite): replacing the stochastic all-in pot award
// with its exact equity expectation is an unbiased control variate that must
// shrink the CI versus the raw estimator on the same deals, while conserving.
TEST(ArenaTest, AivatReducesVariance) {
  auto make_rule = []() {
    AIConfig c;
    c.strategy = AIStrategyType::RuleBased;
    c.random_seed = 11;
    return CreateAIEngine(c);
  };
  AIConfig rand_cfg;
  rand_cfg.random_seed = 22;

  auto rule_raw = make_rule();
  RandomAgent rand_raw{rand_cfg};
  MatchResult raw = RunHeadsUp(*rule_raw, rand_raw, BenchConfig(3000, 2024));

  auto rule_av = make_rule();
  RandomAgent rand_av{rand_cfg};
  MatchConfig av = BenchConfig(3000, 2024);
  av.aivat = true;
  MatchResult r = RunHeadsUp(*rule_av, rand_av, av);

  EXPECT_TRUE(raw.chips_conserved);
  EXPECT_TRUE(r.chips_conserved);
  EXPECT_TRUE(r.aivat_applied);
  EXPECT_FALSE(raw.aivat_applied);
  EXPECT_GT(r.adjusted_hands, 0);
  EXPECT_GT(raw.ci95, 0.0);
  EXPECT_LT(r.ci95, raw.ci95) << "aivat ci=" << r.ci95 << " raw ci=" << raw.ci95;
  // net_by_seat still records REALIZED chips, so the adjustment cannot break
  // conservation nor change the actual chip ledger.
  EXPECT_EQ(r.net_by_seat[0] + r.net_by_seat[1], 0);
  EXPECT_EQ(r.net_by_seat, raw.net_by_seat);
}

// Unbiasedness: the AIVAT and raw estimators are two correlated estimates of the
// same win rate on the same deals, so they must agree within the combined 95%
// band (loose bound to avoid flakiness).
TEST(ArenaTest, AivatIsUnbiasedWithinCI) {
  auto make_rule = []() {
    AIConfig c;
    c.strategy = AIStrategyType::RuleBased;
    c.random_seed = 11;
    return CreateAIEngine(c);
  };
  AIConfig rand_cfg;
  rand_cfg.random_seed = 22;

  auto rule_raw = make_rule();
  RandomAgent rand_raw{rand_cfg};
  MatchResult raw = RunHeadsUp(*rule_raw, rand_raw, BenchConfig(3000, 2024));

  auto rule_av = make_rule();
  RandomAgent rand_av{rand_cfg};
  MatchConfig av = BenchConfig(3000, 2024);
  av.aivat = true;
  MatchResult r = RunHeadsUp(*rule_av, rand_av, av);

  EXPECT_LT(std::abs(r.mbb_per_100 - raw.mbb_per_100), raw.ci95 + r.ci95)
      << "aivat mbb=" << r.mbb_per_100 << " raw mbb=" << raw.mbb_per_100;
}

// AIVAT uses exact enumeration (flop/turn) or fixed-seed MC (preflop), so a
// fixed match seed must reproduce every reported figure exactly.
TEST(ArenaTest, AivatIsDeterministic) {
  auto run = []() {
    AIConfig ca;
    ca.strategy = AIStrategyType::RuleBased;
    ca.random_seed = 11;
    auto a = CreateAIEngine(ca);
    AIConfig cb;
    cb.random_seed = 22;
    RandomAgent b{cb};
    MatchConfig cfg = BenchConfig(800, 2024);
    cfg.aivat = true;
    return RunHeadsUp(*a, b, cfg);
  };
  MatchResult r1 = run();
  MatchResult r2 = run();
  EXPECT_EQ(r1.net_by_seat, r2.net_by_seat);
  EXPECT_EQ(r1.adjusted_hands, r2.adjusted_hands);
  EXPECT_DOUBLE_EQ(r1.mbb_per_100, r2.mbb_per_100);
  EXPECT_DOUBLE_EQ(r1.ci95, r2.ci95);
}

// The per-street CV must fire on forced runouts where one player is all-in and
// the caller still has chips behind (dealt street-by-street, active_not_allin
// == 1) — a case the earlier all-in-only version missed. Deep stacks make such
// spots common. We assert the adjustment triggers, still conserves chips, and
// tightens the CI without shifting the estimate outside the combined band.
TEST(ArenaTest, AivatPerStreetCoversCallerBehind) {
  auto make_rule = []() {
    AIConfig c;
    c.strategy = AIStrategyType::RuleBased;
    c.random_seed = 11;
    return CreateAIEngine(c);
  };
  AIConfig rand_cfg;
  rand_cfg.random_seed = 22;

  auto rule_raw = make_rule();
  RandomAgent rand_raw{rand_cfg};
  MatchConfig raw_cfg = BenchConfig(3000, 4242);
  raw_cfg.starting_stack = 400 * raw_cfg.table.big_blind;  // deep => caller-behind
  MatchResult raw = RunHeadsUp(*rule_raw, rand_raw, raw_cfg);

  auto rule_av = make_rule();
  RandomAgent rand_av{rand_cfg};
  MatchConfig av = raw_cfg;
  av.aivat = true;
  MatchResult r = RunHeadsUp(*rule_av, rand_av, av);

  EXPECT_TRUE(raw.chips_conserved);
  EXPECT_TRUE(r.chips_conserved);
  EXPECT_TRUE(r.aivat_applied);
  EXPECT_GT(r.adjusted_hands, 0);
  EXPECT_GT(raw.ci95, 0.0);
  EXPECT_LT(r.ci95, raw.ci95) << "aivat ci=" << r.ci95 << " raw ci=" << raw.ci95;
  EXPECT_EQ(r.net_by_seat, raw.net_by_seat);
  EXPECT_EQ(r.net_by_seat[0] + r.net_by_seat[1], 0);
  EXPECT_LT(std::abs(r.mbb_per_100 - raw.mbb_per_100), raw.ci95 + r.ci95)
      << "aivat mbb=" << r.mbb_per_100 << " raw mbb=" << raw.mbb_per_100;
}

// --- Baseline agents (honest, deterministic sparring partners) ---

// Build a one-seat Observation for the viewer with the given stack so that a
// Maniac's all-in sizing has a chip ceiling to clamp to.
poker_engine::game::Observation ViewerObs(int32_t id, Chips chips, Chips current_bet) {
  poker_engine::game::Observation obs;
  obs.viewer_id = id;
  poker_engine::game::PlayerView pv;
  pv.id = id;
  pv.chips = chips;
  pv.bet_info.current_bet = current_bet;
  obs.players.push_back(pv);
  return obs;
}

GameAction MakeAction(ActionType type, Chips amount = 0) {
  GameAction a;
  a.type = type;
  a.amount = amount;
  return a;
}

// CallStation must never choose BET/RAISE, and must prefer CHECK > CALL > FOLD.
TEST(ArenaTest, CallStationNeverRaises) {
  CallStationAgent agent{AIConfig{}};
  auto obs = ViewerObs(1, 5000, 200);

  // Full menu: check available => check (free), never bet/raise.
  {
    std::vector<GameAction> legal = {MakeAction(ActionType::FOLD), MakeAction(ActionType::CHECK),
                                     MakeAction(ActionType::BET, 100),
                                     MakeAction(ActionType::RAISE, 400)};
    GameAction act = agent.Decide({obs, 1, legal}).action;
    EXPECT_EQ(act.type, ActionType::CHECK);
  }
  // No check but facing a bet: call rather than fold.
  {
    std::vector<GameAction> legal = {MakeAction(ActionType::FOLD), MakeAction(ActionType::CALL, 200),
                                     MakeAction(ActionType::RAISE, 400)};
    GameAction act = agent.Decide({obs, 1, legal}).action;
    EXPECT_EQ(act.type, ActionType::CALL);
  }
  // Only fold is legal: fold.
  {
    std::vector<GameAction> legal = {MakeAction(ActionType::FOLD)};
    GameAction act = agent.Decide({obs, 1, legal}).action;
    EXPECT_EQ(act.type, ActionType::FOLD);
  }
}

// Maniac must prefer RAISE > BET and shove to all-in (stack + current bet).
TEST(ArenaTest, ManiacPrefersAggression) {
  ManiacAgent agent{AIConfig{}};
  auto obs = ViewerObs(1, 5000, 200);
  const Chips all_in = 5000 + 200;

  {
    std::vector<GameAction> legal = {MakeAction(ActionType::FOLD), MakeAction(ActionType::CHECK),
                                     MakeAction(ActionType::BET, 100),
                                     MakeAction(ActionType::RAISE, 400)};
    GameAction act = agent.Decide({obs, 1, legal}).action;
    EXPECT_EQ(act.type, ActionType::RAISE);
    EXPECT_EQ(act.amount, all_in);
  }
  // No raise available: bet, sized all-in.
  {
    std::vector<GameAction> legal = {MakeAction(ActionType::FOLD), MakeAction(ActionType::CHECK),
                                     MakeAction(ActionType::BET, 100)};
    GameAction act = agent.Decide({obs, 1, legal}).action;
    EXPECT_EQ(act.type, ActionType::BET);
    EXPECT_EQ(act.amount, all_in);
  }
  // No aggression available: call.
  {
    std::vector<GameAction> legal = {MakeAction(ActionType::FOLD), MakeAction(ActionType::CALL, 200)};
    GameAction act = agent.Decide({obs, 1, legal}).action;
    EXPECT_EQ(act.type, ActionType::CALL);
  }
}

// The round-robin leaderboard derives agent 1's win rate by negating agent 0's
// per-hand sample (heads-up is zero-sum). Validate both the stat-pooling formula
// and that negation, plus per-pair chip conservation, over the baseline set.
TEST(ArenaTest, RoundRobinIsAntisymmetricAndConserves) {
  AIConfig cr;
  cr.random_seed = 22;
  auto make_agent = [](const std::string& kind, int seed) -> std::shared_ptr<IAIEngine> {
    AIConfig c;
    c.random_seed = seed;
    if (kind == "random") return std::make_shared<RandomAgent>(c);
    if (kind == "call") return std::make_shared<CallStationAgent>(c);
    return std::make_shared<ManiacAgent>(c);
  };
  const std::vector<std::string> kinds = {"random", "call", "maniac"};

  for (size_t i = 0; i < kinds.size(); ++i) {
    for (size_t j = i + 1; j < kinds.size(); ++j) {
      auto a = make_agent(kinds[i], 1);
      auto b = make_agent(kinds[j], 2);
      MatchResult r = RunHeadsUp(*a, *b, BenchConfig(400, 900 + i * 10 + j));
      EXPECT_TRUE(r.chips_conserved) << kinds[i] << " vs " << kinds[j];
      ASSERT_EQ(r.net_by_seat.size(), 2u);
      EXPECT_EQ(r.net_by_seat[0] + r.net_by_seat[1], 0);
      ASSERT_GT(r.sample_n, 0);

      const double a0 = r.sample_sum / static_cast<double>(r.sample_n) * 100.0;
      const double a1 = -r.sample_sum / static_cast<double>(r.sample_n) * 100.0;
      EXPECT_NEAR(a0, r.mbb_per_100, 1e-6);
      EXPECT_DOUBLE_EQ(a1, -r.mbb_per_100);  // matrix negation identity
    }
  }
}

// Baseline matches (with the new agents) must reproduce exactly for a fixed seed.
TEST(ArenaTest, RoundRobinIsDeterministic) {
  auto run = []() {
    CallStationAgent a{AIConfig{}};
    ManiacAgent b{AIConfig{}};
    return RunHeadsUp(a, b, BenchConfig(300, 4242));
  };
  MatchResult r1 = run();
  MatchResult r2 = run();
  EXPECT_EQ(r1.net_by_seat, r2.net_by_seat);
  EXPECT_EQ(r1.hands_played, r2.hands_played);
  EXPECT_DOUBLE_EQ(r1.mbb_per_100, r2.mbb_per_100);
  EXPECT_DOUBLE_EQ(r1.ci95, r2.ci95);
}

LbrConfig LbrBench(int hands, uint64_t seed) {
  LbrConfig cfg;
  cfg.hands = hands;
  cfg.seed = seed;
  cfg.table = SmallTable();
  return cfg;
}

// A fold/call-only LBR provably exploits an over-aggressive Maniac: by calling
// with the right hands and folding trash at the correct pot odds, it wins at a
// rate whose 95% CI lower bound is strictly positive. This is a real lower
// bound on the Maniac's full-game exploitability. (A passive CallStation is NOT
// exploitable by a non-betting LBR — punishing over-calling requires betting.)
TEST(ArenaTest, LbrExploitsManiac) {
  ManiacAgent live{AIConfig{}};
  ManiacAgent probe{AIConfig{}};
  LbrResult r = RunLbr(live, probe, LbrBench(1500, 1));

  EXPECT_TRUE(r.chips_conserved);
  EXPECT_EQ(r.hands_played, 1500);
  ASSERT_GT(r.sample_n, 0);
  EXPECT_GT(r.mbb_per_100 - r.ci95, 0.0);  // statistically significant exploit
}

// RunLbr is fully deterministic for a fixed seed (equity is exact or fixed-seed
// MC; belief updates are deterministic given the probe agent).
TEST(ArenaTest, LbrIsDeterministic) {
  auto run = []() {
    ManiacAgent live{AIConfig{}};
    ManiacAgent probe{AIConfig{}};
    return RunLbr(live, probe, LbrBench(200, 4242));
  };
  LbrResult r1 = run();
  LbrResult r2 = run();
  EXPECT_EQ(r1.sample_n, r2.sample_n);
  EXPECT_EQ(r1.hands_played, r2.hands_played);
  EXPECT_DOUBLE_EQ(r1.mbb_per_100, r2.mbb_per_100);
  EXPECT_DOUBLE_EQ(r1.ci95, r2.ci95);
  EXPECT_DOUBLE_EQ(r1.sample_sum, r2.sample_sum);
}

// v0.9: with betting enabled (the default), LBR value-bets on checked-to nodes
// and provably exploits a passive CallStation — the very opponent a fold/call
// LBR (v0.8) could not beat, since punishing over-calling requires betting. The
// 95% CI lower bound is strictly positive: a real lower bound on the station's
// full-game exploitability.
TEST(ArenaTest, LbrExploitsCallStation) {
  CallStationAgent live{AIConfig{}};
  CallStationAgent probe{AIConfig{}};
  LbrResult r = RunLbr(live, probe, LbrBench(250, 1));

  EXPECT_TRUE(r.chips_conserved);
  EXPECT_EQ(r.hands_played, 250);
  ASSERT_GT(r.sample_n, 0);
  EXPECT_GT(r.mbb_per_100 - r.ci95, 0.0);  // statistically significant exploit
}

// v0.9: betting is what closes the v0.8 CallStation gap. A fold/call-only LBR
// (cfg.bet == false) cannot profit from a station, whereas the value-betting LBR
// wins at a dramatically higher rate over the same deals. Both conserve chips.
TEST(ArenaTest, LbrBettingTightensVsCallStation) {
  auto run = [](bool bet) {
    CallStationAgent live{AIConfig{}};
    CallStationAgent probe{AIConfig{}};
    LbrConfig cfg = LbrBench(250, 1);
    cfg.bet = bet;
    return RunLbr(live, probe, cfg);
  };
  LbrResult no_bet = run(false);
  LbrResult with_bet = run(true);

  EXPECT_TRUE(no_bet.chips_conserved);
  EXPECT_TRUE(with_bet.chips_conserved);
  EXPECT_GT(with_bet.mbb_per_100, no_bet.mbb_per_100);
}

}  // namespace
