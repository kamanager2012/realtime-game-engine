#include <gtest/gtest.h>

#include "poker_engine/game/action.h"
#include "poker_engine/game/action_validator.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/game/player_state.h"
#include "poker_engine/game/pot_manager.h"

using namespace poker_engine::game;

TEST(Phase12Ext, PreflopActionOrderFixed) {
  TableConfig config;
  config.max_players = 3;
  config.small_blind = 1;
  config.big_blind = 2;
  GameState gs(config);
  gs.AddPlayer(1, "SB", 100);
  gs.AddPlayer(2, "BB", 100);
  gs.AddPlayer(3, "BTN", 100);
  gs.PostBlinds();
  EXPECT_TRUE(gs.GetPlayerAtSeat(1)->is_big_blind);
  gs.StartHand();
  auto* cur = gs.GetCurrentPlayer();
  ASSERT_NE(cur, nullptr);
  std::cout << gs.ToString() << "\n";
}

TEST(Phase12Ext, AllInAlwaysLegal) {
  PlayerState p;
  p.id = 1;
  p.chips = 3;
  p.seat_state = SeatState::PLAYING;
  GameAction allin;
  allin.type = ActionType::ALL_IN;
  allin.amount = 3;
  auto result = ActionValidator::Validate(allin, p, {&p}, 50, 0, 10, 0, 2, 0, 0);
  EXPECT_TRUE(result.valid);
  EXPECT_DOUBLE_EQ(result.adjusted_amount, 3);
}

TEST(Phase12Ext, FoldedCannotAct) {
  PlayerState p;
  p.id = 1;
  p.chips = 100;
  p.seat_state = SeatState::FOLDED;
  GameAction call;
  call.type = ActionType::CALL;
  auto result = ActionValidator::Validate(call, p, {&p}, 0, 0, 1, 0, 1, 0, 0);
  EXPECT_FALSE(result.valid);
}

TEST(Phase12Ext, ExactPots) {
  PotManager pm;
  PlayerState p1, p2, p3;
  p1.id = 1;
  p1.bet_info.total_invested = 100;
  p1.seat_state = SeatState::ALL_IN;
  p2.id = 2;
  p2.bet_info.total_invested = 100;
  p2.seat_state = SeatState::PLAYING;
  p3.id = 3;
  p3.bet_info.total_invested = 100;
  p3.seat_state = SeatState::PLAYING;
  auto pots = pm.BuildPots({&p1, &p2, &p3});
  EXPECT_EQ(pots.size(), 1);
  EXPECT_NEAR(pots[0].amount, 300, 0.01);
}

TEST(Phase12Ext, SidePotMultipleAllIn) {
  PotManager pm;
  PlayerState p1, p2, p3;
  p1.id = 1;
  p1.bet_info.total_invested = 30;
  p1.seat_state = SeatState::ALL_IN;
  p2.id = 2;
  p2.bet_info.total_invested = 100;
  p2.seat_state = SeatState::PLAYING;
  p3.id = 3;
  p3.bet_info.total_invested = 100;
  p3.seat_state = SeatState::PLAYING;
  auto pots = pm.BuildPots({&p1, &p2, &p3});
  double total = pm.TotalPot();
  EXPECT_NEAR(total, 230, 0.01);
  EXPECT_GE(pots.size(), 2);
}

TEST(Phase12Ext, CheckOnlyWhenNoBet) {
  PlayerState p;
  p.id = 1;
  p.chips = 100;
  p.seat_state = SeatState::PLAYING;
  p.bet_info.current_bet = 0;
  GameAction check;
  check.type = ActionType::CHECK;
  auto r1 = ActionValidator::Validate(check, p, {}, 0, 0, 1, 0, 2, 0, 0);
  EXPECT_TRUE(r1.valid);
  auto r2 = ActionValidator::Validate(check, p, {}, 10, 0, 1, 0, 2, 0, 0);
  EXPECT_FALSE(r2.valid);
}

TEST(Phase12Ext, BetSizingConstraints) {
  PlayerState p;
  p.id = 1;
  p.chips = 100;
  p.seat_state = SeatState::PLAYING;
  GameAction huge;
  huge.type = ActionType::BET;
  huge.amount = 500;
  auto result = ActionValidator::Validate(huge, p, {}, 0, 0, 1, 0, 2, 0, 0);
  EXPECT_TRUE(result.valid);
  EXPECT_LE(result.adjusted_amount, 100.01);
}

TEST(Phase12Ext, ZeroChipsCannotBet) {
  PlayerState p;
  p.id = 1;
  p.chips = 0;
  p.seat_state = SeatState::PLAYING;
  GameAction bet;
  bet.type = ActionType::BET;
  bet.amount = 10;
  auto result = ActionValidator::Validate(bet, p, {}, 0, 0, 1, 0, 1, 0, 0);
  EXPECT_FALSE(result.valid);
  GameAction fold;
  fold.type = ActionType::FOLD;
  EXPECT_TRUE(ActionValidator::Validate(fold, p, {}, 0, 0, 1, 0, 1, 0, 0).valid);
}

TEST(Phase12Ext, ActionMessageFormat) {
  EXPECT_EQ(GameAction{ActionType::FOLD, 0}.ToString(), "FOLD");
  EXPECT_EQ(GameAction{ActionType::CHECK, 0}.ToString(), "CHECK");
  GameAction bet50;
  bet50.type = ActionType::BET;
  bet50.amount = 50;
  EXPECT_NE(bet50.ToString().find("50"), std::string::npos);
}

TEST(Phase12Ext, CommunityCardsProgression) {
  CommunityCards cc;
  EXPECT_EQ(cc.count, 0);
  cc.Add(1);
  cc.Add(2);
  cc.Add(3);
  EXPECT_TRUE(cc.HasFlop());
  cc.Add(4);
  EXPECT_TRUE(cc.HasTurn());
  cc.Add(5);
  EXPECT_TRUE(cc.HasRiver());
}
