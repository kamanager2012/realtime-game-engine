// CFR inference parity: the strategy a CFR agent produces must be identical
// whether it reads the full GameState or a redacted Observation. Both paths
// funnel through CFREngine::ComputeInfosetKeyFromParts, so the infoset key —
// and therefore the sampled action distribution — must not drift.
#include <gtest/gtest.h>

#include "poker_engine/cfr/cfr_engine.h"
#include "poker_engine/game/game_state.h"

namespace {

using poker_engine::cfr::Action;
using poker_engine::cfr::CFREngine;
using poker_engine::cfr::CFROptions;
using poker_engine::game::GameState;
using poker_engine::game::TableConfig;

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

// Walk a live hand; at each decision point the GameState-keyed strategy and the
// Observation-keyed strategy for the acting player must be byte-identical.
TEST(CfrObservationTest, StrategyParityBetweenGameStateAndObservation) {
  CFROptions options;
  options.config.num_iterations = 200;
  options.check_interval = 50;
  CFREngine engine(options);
  engine.Initialize();
  engine.Train(200);  // populate some nodes so we exercise the node-hit path too

  TableConfig t = SmallTable();
  GameState state(t);
  state.SetDeterministicDeckSeed(7);
  state.AddPlayerAtSeat(1, "A", 20000, 0);
  state.AddPlayerAtSeat(2, "B", 20000, 1);
  ASSERT_TRUE(state.StartHand());

  int compared = 0;
  int guard = 0;
  while (state.IsHandInProgress() && guard++ < 2000) {
    int32_t cur = state.GetCurrentPlayerId();
    if (cur < 0) break;

    auto from_state = engine.GetStrategyForState(state, cur);
    auto from_obs = engine.GetStrategyForState(state.ObserveFor(cur), cur);

    ASSERT_EQ(from_state.size(), from_obs.size());
    for (size_t i = 0; i < from_state.size(); ++i) {
      EXPECT_EQ(from_state[i].first, from_obs[i].first);
      EXPECT_DOUBLE_EQ(from_state[i].second, from_obs[i].second);
    }
    compared++;

    auto legal = state.LegalActions(cur);
    ASSERT_FALSE(legal.empty());
    auto action = legal.front();
    action.player_id = cur;
    if (!state.ProcessAction(cur, action)) break;
  }
  EXPECT_GT(compared, 0);
}

}  // namespace
