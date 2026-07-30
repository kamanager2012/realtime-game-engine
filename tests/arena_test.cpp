// Arena (agent research seam) tests: legality, chip conservation,
// determinism, and baseline separation.
#include <gtest/gtest.h>

#include "poker_engine/arena/match_runner.h"
#include "poker_engine/arena/random_agent.h"
#include "poker_engine/game/action_validator.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/network/ai_engine.h"

namespace {

using poker_engine::arena::MatchConfig;
using poker_engine::arena::MatchResult;
using poker_engine::arena::RandomAgent;
using poker_engine::arena::RunHeadsUp;
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

}  // namespace
