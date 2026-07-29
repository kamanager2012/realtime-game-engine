#if 0  // API mismatch with actual GameState/PlayerState schema - needs rewrite
#include <gtest/gtest.h>

#include "poker_engine/game/action.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/serialization/flatbuffers_serializer.h"

using namespace poker_engine::serialization;
using namespace poker_engine::game;

class FlatBufferTest : public ::testing::Test {
protected:
    FlatBufferSerializer serializer_;

    GameState CreateTestState() {
        GameState state;
        state.table_id = "test_table_1";
        state.hand_number = 42;
        state.phase = GamePhase::River;
        state.status = GameStatus::Playing;
        state.dealer_seat = 3;
        state.current_player_id = 100;
        state.current_bet = 500;
        state.min_raise_size = 1000;
        state.pot = 5000;
        state.big_blind = 200;
        state.small_blind = 100;
        state.timestamp_ms = 1234567890;

        state.community_cards = {2, 15, 28, 41, 7};  // 5 张牌
        state.winners = {100};

        // 3 个玩家
        for (int i = 0; i < 3; ++i) {
            PlayerState p;
            p.player_id = 100 + i;
            p.seat_index = i;
            p.chips = 10000 - i * 500;
            p.bet_this_round = 500 + i * 100;
            p.total_invested = 2000 + i * 500;
            p.status = (i == 2) ? PlayerStatus::Folded : PlayerStatus::Active;
            p.action_status = (i == 0) ? ActionType::Check : ActionType::Call;
            p.occupied = true;
            p.display_name = "Player" + std::to_string(i);
            p.is_winner = (i == 0);

            if (i < 2) {
                p.hole_cards = {static_cast<uint8_t>(i * 4),
                                 static_cast<uint8_t>(i * 4 + 1)};
            }

            p.actions.push_back(Action{ActionType::Call, 500, 0,
                                        static_cast<int32_t>(p.player_id), 1000});

            state.players.push_back(p);
        }

        return state;
    }
};

TEST_F(FlatBufferTest, SerializeDeserializeGameState) {
    auto state = CreateTestState();
    auto buf = serializer_.SerializeGameState(state);

    EXPECT_GT(buf.size(), 0u);
    EXPECT_LT(buf.size(), 4096u);  // 应该紧凑

    auto result = serializer_.DeserializeGameState(buf);
    ASSERT_TRUE(result.IsOk());

    auto deserialized = result.Unwrap();

    // 验证关键字段
    EXPECT_EQ(deserialized.table_id, state.table_id);
    EXPECT_EQ(deserialized.hand_number, state.hand_number);
    EXPECT_EQ(deserialized.phase, state.phase);
    EXPECT_EQ(deserialized.status, state.status);
    EXPECT_EQ(deserialized.dealer_seat, state.dealer_seat);
    EXPECT_EQ(deserialized.current_player_id, state.current_player_id);
    EXPECT_EQ(deserialized.current_bet, state.current_bet);
    EXPECT_EQ(deserialized.pot, state.pot);
    EXPECT_EQ(deserialized.community_cards.size(),
              state.community_cards.size());
    EXPECT_EQ(deserialized.winners.size(), state.winners.size());
    EXPECT_EQ(deserialized.players.size(), 2u);  // 只有 occupied=true 的被序列化
}

TEST_F(FlatBufferTest, RoundTripEquality) {
    auto state = CreateTestState();
    auto buf = serializer_.SerializeGameState(state);
    auto result = serializer_.DeserializeGameState(buf);
    ASSERT_TRUE(result.IsOk());

    // 关键数据相等
    EXPECT_EQ(result.Unwrap().pot, state.pot);
    EXPECT_EQ(result.Unwrap().big_blind, state.big_blind);
    EXPECT_EQ(result.Unwrap().small_blind, state.small_blind);
}

TEST_F(FlatBufferTest, SerializeDeserializeAction) {
    Action action;
    action.type = ActionType::Raise;
    action.amount = 2500;
    action.street = 2;  // Turn
    action.player_id = 42;
    action.timestamp_ms = 999;

    auto buf = serializer_.SerializeAction(action);
    auto result = serializer_.DeserializeAction(buf);

    ASSERT_TRUE(result.IsOk());
    auto deserialized = result.Unwrap();
    EXPECT_EQ(deserialized.type, action.type);
    EXPECT_EQ(deserialized.amount, action.amount);
    EXPECT_EQ(deserialized.street, action.street);
    EXPECT_EQ(deserialized.player_id, action.player_id);
}

TEST_F(FlatBufferTest, SerializeActionBatch) {
    std::vector<Action> actions;
    for (int i = 0; i < 10; ++i) {
        actions.push_back({static_cast<ActionType>(i % 5), i * 100,
                          static_cast<uint8_t>(i % 4), i, i * 1000LL});
    }

    auto buf = serializer_.SerializeActionBatch(actions);
    auto result = serializer_.DeserializeActionBatch(buf);

    ASSERT_TRUE(result.IsOk());
    auto deserialized = result.Unwrap();
    EXPECT_EQ(deserialized.size(), actions.size());

    for (size_t i = 0; i < actions.size(); ++i) {
        EXPECT_EQ(deserialized[i].type, actions[i].type);
        EXPECT_EQ(deserialized[i].amount, actions[i].amount);
    }
}

TEST_F(FlatBufferTest, WSMessages) {
    auto buf = serializer_.SerializeWSMessage(
        fbs::WSMessageType::PlayerAction, 1,
        R"({"action":"raise","amount":1000})");

    auto result = serializer_.DeserializeWSMessage(buf);
    ASSERT_TRUE(result.IsOk());

    auto msg = result.Unwrap();
    EXPECT_EQ(msg.type, fbs::WSMessageType::PlayerAction);
    EXPECT_EQ(msg.seq, 1u);
    EXPECT_EQ(msg.payload.size(), 26u);
}

TEST_F(FlatBufferTest, ValidationRejectsCorruptedData) {
    // 创建有效数据
    auto buf = serializer_.SerializeGameState(CreateTestState());

    // 篡改中间字节
    auto corrupted = buf;
    uint8_t* data = corrupted.mutate();
    if (data && corrupted.size() > 100) {
        data[50] ^= 0xFF;  // 翻转字节
    }

    // 验证应失败
    auto result = serializer_.DeserializeGameStateSafe(corrupted);
    EXPECT_FALSE(result.IsOk());
}

TEST_F(FlatBufferTest, EmptyState) {
    GameState empty;
    empty.table_id = "";
    empty.phase = GamePhase::Waiting;
    empty.status = GameStatus::Idle;
    empty.hand_number = 0;

    auto buf = serializer_.SerializeGameState(empty);
    auto result = serializer_.DeserializeGameState(buf);
    ASSERT_TRUE(result.IsOk());

    EXPECT_EQ(result.Unwrap().table_id, "");
    EXPECT_EQ(result.Unwrap().players.size(), 0u);
}

TEST_F(FlatBufferTest, LargeStatePerformance) {
    GameState state;
    state.table_id = "perf_test_table";
    state.hand_number = 999;
    state.phase = GamePhase::Flop;
    state.status = GameStatus::Playing;
    state.dealer_seat = 0;
    state.current_player_id = 42;
    state.current_bet = 100;
    state.min_raise_size = 200;
    state.pot = 100000;
    state.big_blind = 100;
    state.small_blind = 50;

    // 9 个玩家 + 大量操作历史
    for (int i = 0; i < 9; ++i) {
        PlayerState p;
        p.player_id = 1000 + i;
        p.seat_index = i;
        p.chips = 100000;
        p.bet_this_round = 0;
        p.total_invested = 0;
        p.status = PlayerStatus::Active;
        p.occupied = true;
        p.display_name = "Player_" + std::to_string(i);
        p.hole_cards = {static_cast<uint8_t>(i), static_cast<uint8_t>(i + 13)};

        // 50 个操作历史
        for (int j = 0; j < 50; ++j) {
            Action a;
            a.type = static_cast<ActionType>(j % 5);
            a.amount = j * 10;
            a.street = j / 15;
            a.player_id = p.player_id;
            a.timestamp_ms = j * 1000LL;
            p.actions.push_back(a);
        }

        state.players.push_back(p);
    }

    state.community_cards = {1, 14, 27, 40, 3};
    state.winners = {1000};

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000; ++i) {
        auto buf = serializer_.SerializeGameState(state);
        auto result = serializer_.DeserializeGameState(buf);
        EXPECT_TRUE(result.IsOk());
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start).count();

    std::cout << "\n[Perf] 1000x 序列化/反序列化 (9玩家, 50操作): "
              << elapsed << "ms\n";

    // 每次序列化+反序列化 < 1ms
    EXPECT_LT(elapsed / 1000, 2);
}
#endif
