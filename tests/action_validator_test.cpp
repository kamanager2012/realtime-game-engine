#include "poker_engine/game/action_validator.h"

#include <gtest/gtest.h>

#include "poker_engine/game/action.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/game/player_state.h"

using namespace poker_engine::game;

class ActionValidatorTest : public ::testing::Test {
 protected:
  void SetUp() override { validator_ = std::make_unique<ActionValidator>(); }

  GameState CreateState(int num_players) {
    GameState state;
    state.table_id = "test_table";
    state.status = GameStatus::Playing;
    state.phase = GamePhase::Preflop;
    state.dealer_seat = 0;
    state.current_bet = 2;
    state.min_raise_size = 2;
    state.pot = 3;

    state.players.resize(num_players);
    for (int i = 0; i < num_players; ++i) {
      state.players[i].player_id = 100 + i;
      state.players[i].seat_index = static_cast<uint8_t>(i);
      state.players[i].chips = 1000;
      state.players[i].status = PlayerStatus::Active;
      state.players[i].action_status = ActionStatus::None;
      state.players[i].occupied = true;
    }
    return state;
  }

  std::unique_ptr<ActionValidator> validator_;
};

TEST_F(ActionValidatorTest, FoldAlwaysValid) {
  auto state = CreateState(3);
  state.phase = GamePhase::Preflop;
  state.current_player_id = 100;
  state.players[0].action_status = ActionStatus::None;

  auto result = validator_->Validate(state, 100, ActionType::Fold);
  EXPECT_TRUE(result.valid);
}

TEST_F(ActionValidatorTest, WrongPlayerTurn) {
  auto state = CreateState(3);
  state.current_player_id = 100;

  auto result = validator_->Validate(state, 101, ActionType::Call, 2);
  EXPECT_FALSE(result.valid);
  EXPECT_NE(result.error_message.find("turn"), std::string::npos);
}

TEST_F(ActionValidatorTest, CallWithNothingToCallFails) {
  auto state = CreateState(3);
  state.current_player_id = 100;
  state.current_bet = 0;

  auto result = validator_->Validate(state, 100, ActionType::Call, 0);
  EXPECT_FALSE(result.valid);
  EXPECT_NE(result.error_message.find("Nothing to call"), std::string::npos);
}

TEST_F(ActionValidatorTest, P03_BigBlindCanCallPreflop) {
  auto state = CreateState(3);
  state.dealer_seat = 0;
  state.current_bet = 2;
  state.phase = GamePhase::Preflop;

  state.players[0].status = PlayerStatus::Folded;
  state.players[0].action_status = ActionStatus::Folded;

  state.players[1].bet_this_round = 1;
  state.players[1].action_status = ActionStatus::Checked;

  state.players[2].bet_this_round = 2;
  state.players[2].action_status = ActionStatus::Called;
  state.current_player_id = 102;

  auto result = validator_->Validate(state, 102, ActionType::Call, 0);
  EXPECT_TRUE(result.valid) << "BB should be able to call preflop. Error: " << result.error_message;
}

TEST_F(ActionValidatorTest, P03_BigBlindCanRaisePreflop) {
  auto state = CreateState(3);
  state.dealer_seat = 0;
  state.current_bet = 2;
  state.phase = GamePhase::Preflop;

  state.players[0].status = PlayerStatus::Folded;
  state.players[1].action_status = ActionStatus::Checked;
  state.players[2].bet_this_round = 2;
  state.players[2].action_status = ActionStatus::Called;
  state.current_player_id = 102;

  auto result = validator_->Validate(state, 102, ActionType::Raise, 6);
  EXPECT_TRUE(result.valid) << "BB should be able to raise preflop. Error: "
                            << result.error_message;
}

TEST_F(ActionValidatorTest, P03_BigBlindCannotCheckWhenFacingBet) {
  auto state = CreateState(3);
  state.dealer_seat = 0;
  state.current_bet = 10;
  state.phase = GamePhase::Preflop;

  state.players[0].bet_this_round = 10;
  state.players[0].action_status = ActionStatus::Raised;
  state.players[1].bet_this_round = 1;
  state.players[1].action_status = ActionStatus::Called;
  state.players[2].bet_this_round = 2;
  state.players[2].action_status = ActionStatus::Called;
  state.current_player_id = 102;

  auto result = validator_->Validate(state, 102, ActionType::Check);
  EXPECT_FALSE(result.valid);
  EXPECT_NE(result.error_message.find("bet to call"), std::string::npos);
}

TEST_F(ActionValidatorTest, CheckOnlyWhenNoBet) {
  auto state = CreateState(2);
  state.current_bet = 0;
  state.current_player_id = 100;
  state.players[0].bet_this_round = 0;

  auto result = validator_->Validate(state, 100, ActionType::Check);
  EXPECT_TRUE(result.valid);
}

TEST_F(ActionValidatorTest, CheckFailsWhenBetExists) {
  auto state = CreateState(2);
  state.current_bet = 10;
  state.current_player_id = 100;
  state.players[0].bet_this_round = 5;

  auto result = validator_->Validate(state, 100, ActionType::Check);
  EXPECT_FALSE(result.valid);
}

TEST_F(ActionValidatorTest, BetFailsWhenBetExists) {
  auto state = CreateState(2);
  state.current_bet = 10;
  state.current_player_id = 100;

  auto result = validator_->Validate(state, 100, ActionType::Bet, 20);
  EXPECT_FALSE(result.valid);
}

TEST_F(ActionValidatorTest, RaiseBelowMinFails) {
  auto state = CreateState(2);
  state.current_bet = 10;
  state.min_raise_size = 10;
  state.current_player_id = 100;

  auto result = validator_->Validate(state, 100, ActionType::Raise, 5);
  EXPECT_FALSE(result.valid);
  EXPECT_NE(result.error_message.find("minimum"), std::string::npos);
}

TEST_F(ActionValidatorTest, AllInAlwaysValid) {
  auto state = CreateState(2);
  state.current_bet = 10;
  state.current_player_id = 100;
  state.players[0].chips = 15;
  state.players[0].bet_this_round = 5;

  auto result = validator_->Validate(state, 100, ActionType::AllIn, 15);
  EXPECT_TRUE(result.valid);
}

TEST_F(ActionValidatorTest, LegalActionsWhenNoBet) {
  auto state = CreateState(2);
  state.current_bet = 0;
  state.current_player_id = 100;
  state.players[0].bet_this_round = 0;

  auto actions = validator_->GetLegalActions(state, 100);
  EXPECT_FALSE(actions.empty());

  bool has_fold = false, has_check = false, has_bet = false, has_allin = false;
  for (auto& [action, amount] : actions) {
    if (action == ActionType::Fold) has_fold = true;
    if (action == ActionType::Check) has_check = true;
    if (action == ActionType::Bet) has_bet = true;
    if (action == ActionType::AllIn) has_allin = true;
  }
  EXPECT_TRUE(has_fold);
  EXPECT_TRUE(has_check);
  EXPECT_TRUE(has_bet);
  EXPECT_TRUE(has_allin);
}

TEST_F(ActionValidatorTest, LegalActionsWhenFacingBet) {
  auto state = CreateState(2);
  state.current_bet = 10;
  state.current_player_id = 100;
  state.players[0].bet_this_round = 5;

  auto actions = validator_->GetLegalActions(state, 100);

  bool has_fold = false, has_call = false, has_raise = false;
  for (auto& [action, amount] : actions) {
    if (action == ActionType::Fold) has_fold = true;
    if (action == ActionType::Call) has_call = true;
    if (action == ActionType::Raise) has_raise = true;
  }
  EXPECT_TRUE(has_fold);
  EXPECT_TRUE(has_call);
  EXPECT_TRUE(has_raise);
}

TEST_F(ActionValidatorTest, CallMoreThanStackBecomesAllIn) {
  auto state = CreateState(2);
  state.current_bet = 100;
  state.current_player_id = 100;
  state.players[0].chips = 30;
  state.players[0].bet_this_round = 0;

  auto result = validator_->Validate(state, 100, ActionType::AllIn, 30);
  EXPECT_TRUE(result.valid);
}

TEST_F(ActionValidatorTest, NegativeAmountRejected) {
  auto state = CreateState(2);
  state.current_bet = 10;
  state.current_player_id = 100;

  auto result = validator_->Validate(state, 100, ActionType::Raise, -5);
  EXPECT_FALSE(result.valid);
}

TEST_F(ActionValidatorTest, SixMaxPreflopActionOrder) {
  auto state = CreateState(6);
  state.dealer_seat = 0;
  state.current_bet = 2;
  state.phase = GamePhase::Preflop;

  state.players[3].bet_this_round = 10;
  state.players[3].action_status = ActionStatus::Raised;
  state.current_bet = 10;
  state.min_raise_size = 10;
  state.current_player_id = 104;

  state.players[5].status = PlayerStatus::Folded;

  auto result = validator_->Validate(state, 104, ActionType::Call, 8);
  EXPECT_TRUE(result.valid) << "MP should be able to call. Error: " << result.error_message;
}
