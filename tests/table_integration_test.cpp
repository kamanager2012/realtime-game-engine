#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/game/action_validator.h"
#include "poker_engine/game/table.h"

using namespace poker_engine::game;

class TableTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_.table_id = "test_table";
    config_.max_players = 3;
    config_.small_blind = 1;
    config_.big_blind = 2;
    config_.min_buy_in = 20;
    config_.max_buy_in = 200;

    table_ = std::make_unique<Table>(config_);

    events_received_.clear();
    table_->Subscribe(
        [this](const GameState& state, const std::string& event_type, const std::string& detail) {
          events_received_.push_back(event_type);
        });
  }

  TableConfig config_;
  std::unique_ptr<Table> table_;
  std::vector<std::string> events_received_;
};

TEST_F(TableTest, SitDownAndStartHand) {
  EXPECT_TRUE(table_->SitDown(1, 0, 100));
  EXPECT_TRUE(table_->SitDown(2, 1, 100));
  EXPECT_TRUE(table_->SitDown(3, 2, 100));

  table_->StartHand();

  auto& state = table_->GetState();
  EXPECT_EQ(state.status, GameStatus::Playing);
  EXPECT_EQ(state.phase, GamePhase::Preflop);
  EXPECT_EQ(state.hand_number, 1u);
}

TEST_F(TableTest, ThreeHandedPreflopFlow) {
  table_->SitDown(1, 0, 100);
  table_->SitDown(2, 1, 100);
  table_->SitDown(3, 2, 100);

  table_->StartHand();

  auto& state = table_->GetState();

  // BTN fold
  auto result = table_->ProcessAction(1, ActionType::Fold);
  EXPECT_TRUE(result.valid) << "BTN fold failed: " << result.error_message;

  // SB raise to 6
  result = table_->ProcessAction(2, ActionType::Raise, 6);
  EXPECT_TRUE(result.valid) << "SB raise failed: " << result.error_message;

  // BB call
  result = table_->ProcessAction(3, ActionType::Call, 4);
  EXPECT_TRUE(result.valid) << "BB call failed: " << result.error_message;

  EXPECT_EQ(state.phase, GamePhase::Flop);
}

TEST_F(TableTest, P03_ThreeHandedBBCallWorks) {
  table_->SitDown(1, 0, 100);
  table_->SitDown(2, 1, 100);
  table_->SitDown(3, 2, 100);

  table_->StartHand();
  auto& state = table_->GetState();

  // UTG (BTN) call
  EXPECT_TRUE(table_->ProcessAction(1, ActionType::Call, 2).valid);

  // SB call
  EXPECT_TRUE(table_->ProcessAction(2, ActionType::Call, 2).valid);

  // BB call — 核心测试
  auto result = table_->ProcessAction(3, ActionType::Call, 0);
  EXPECT_TRUE(result.valid) << "BB call should work! Error: " << result.error_message;
}

TEST_F(TableTest, AllInWorks) {
  table_->SitDown(1, 0, 20);
  table_->SitDown(2, 1, 100);
  table_->SitDown(3, 2, 100);

  table_->StartHand();

  auto result = table_->ProcessAction(1, ActionType::AllIn, 20);
  EXPECT_TRUE(result.valid) << "All-in failed: " << result.error_message;

  result = table_->ProcessAction(2, ActionType::Call, 20);
  EXPECT_TRUE(result.valid);

  result = table_->ProcessAction(3, ActionType::Call, 20);
  EXPECT_TRUE(result.valid);
}

TEST_F(TableTest, StandUpBetweenHands) {
  table_->SitDown(1, 0, 100);
  table_->SitDown(2, 1, 100);
  table_->SitDown(3, 2, 100);

  EXPECT_TRUE(table_->StandUp(1));
  EXPECT_FALSE(table_->StandUp(1));
}

TEST_F(TableTest, CannotStandUpDuringHand) {
  table_->SitDown(1, 0, 100);
  table_->SitDown(2, 1, 100);
  table_->SitDown(3, 2, 100);

  table_->StartHand();

  EXPECT_FALSE(table_->StandUp(2));
}

TEST_F(TableTest, BlindCollectionOnStart) {
  table_->SitDown(1, 0, 100);
  table_->SitDown(2, 1, 100);
  table_->SitDown(3, 2, 100);

  table_->StartHand();

  auto& state = table_->GetState();
  EXPECT_EQ(state.pot, 3);                // SB(1) + BB(2)
  EXPECT_EQ(state.players[1].chips, 99);  // SB
  EXPECT_EQ(state.players[2].chips, 98);  // BB
}

TEST_F(TableTest, EventsAreEmitted) {
  table_->SitDown(1, 0, 100);
  table_->SitDown(2, 1, 100);
  table_->SitDown(3, 2, 100);

  EXPECT_TRUE(events_received_.empty());

  table_->StartHand();

  EXPECT_FALSE(events_received_.empty());
  EXPECT_EQ(events_received_.front(), "hand_start");
}
