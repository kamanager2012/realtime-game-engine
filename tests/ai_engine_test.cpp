#include "poker_engine/network/ai_engine.h"

#include <gtest/gtest.h>

#include "poker_engine/game/game_state.h"
#include "poker_engine/game/player_state.h"

using namespace poker_engine::ai;
using namespace poker_engine::game;

class AIEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_.strategy_type = "rule_based";
    config_.aggression = 1.0f;
    config_.tightness = 1.0f;
    config_.time_limit_ms = 5000;

    ai_ = CreateAIEngine(config_);
  }

  AIConfig config_;
  std::unique_ptr<IAIEngine> ai_;

  GameState CreateTestState() {
    GameState state;
    state.table_id = "test";
    state.status = GameStatus::Playing;
    state.phase = GamePhase::Preflop;
    state.dealer_seat = 0;
    state.current_bet = 2;
    state.min_raise_size = 2;
    state.pot = 3;
    state.current_player_id = 100;

    state.players.resize(3);
    for (int i = 0; i < 3; ++i) {
      state.players[i].player_id = 100 + i;
      state.players[i].seat_index = static_cast<uint8_t>(i);
      state.players[i].chips = 1000;
      state.players[i].status = PlayerStatus::Active;
      state.players[i].action_status = ActionStatus::None;
      state.players[i].occupied = true;
    }
    state.players[2].bet_this_round = 2;
    state.players[2].action_status = ActionStatus::Called;
    return state;
  }
};

TEST_F(AIEngineTest, InitializeSetsConfig) { EXPECT_NO_THROW(ai_->Initialize(config_)); }

TEST_F(AIEngineTest, DecideReturnsValidAction) {
  auto state = CreateTestState();

  DecisionRequest request;
  request.state = &state;
  request.player_id = 100;
  request.legal_actions = {{ActionType::Fold, 0}, {ActionType::Call, 2}, {ActionType::Raise, 6}};

  auto response = ai_->Decide(request);

  EXPECT_GE(response.decision_time_ms, 0);
  EXPECT_GE(response.confidence, 0.0f);
  EXPECT_LE(response.confidence, 1.0f);
}

TEST_F(AIEngineTest, DecideWithStrongHandRaisesPreflop) {
  auto state = CreateTestState();
  state.players[0].hole_cards = {12, 25};  // 两个 Ace

  DecisionRequest request;
  request.state = &state;
  request.player_id = 100;
  request.legal_actions = {{ActionType::Fold, 0}, {ActionType::Call, 2}, {ActionType::Raise, 6}};

  auto response = ai_->Decide(request);
  EXPECT_TRUE(response.action == ActionType::Raise || response.action == ActionType::Call);
}

TEST_F(AIEngineTest, TightConfigFoldsMore) {
  config_.tightness = 2.0f;
  auto ai = CreateAIEngine(config_);

  auto state = CreateTestState();
  state.players[0].hole_cards = {2, 15};

  DecisionRequest request;
  request.state = &state;
  request.player_id = 100;
  request.legal_actions = {{ActionType::Fold, 0}, {ActionType::Call, 2}, {ActionType::Raise, 6}};

  auto response = ai->Decide(request);
  EXPECT_TRUE(response.action == ActionType::Fold || response.action == ActionType::Call);
}

TEST_F(AIEngineTest, OnHandCompleteDoesNotCrash) {
  auto state = CreateTestState();
  EXPECT_NO_THROW(ai_->OnHandComplete(state));
}

TEST_F(AIEngineTest, DecisionTimeIsMeasured) {
  auto state = CreateTestState();

  DecisionRequest request;
  request.state = &state;
  request.player_id = 100;
  request.legal_actions = {{ActionType::Fold, 0}, {ActionType::Call, 2}};

  auto response = ai_->Decide(request);
  EXPECT_LT(response.decision_time_ms, 100);
}

TEST_F(AIEngineTest, ConfidenceInRange) {
  auto state = CreateTestState();

  DecisionRequest request;
  request.state = &state;
  request.player_id = 100;
  request.legal_actions = {{ActionType::Fold, 0}, {ActionType::Call, 2}, {ActionType::Raise, 6}};

  for (int i = 0; i < 10; ++i) {
    auto response = ai_->Decide(request);
    EXPECT_GE(response.confidence, 0.0f);
    EXPECT_LE(response.confidence, 1.0f);
  }
}
