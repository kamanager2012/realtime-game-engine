#include <gtest/gtest.h>

#include "buy_in_policy.h"
#include "poker_engine/network/game_server.h"

using namespace poker_engine::cli;
using namespace poker_engine::network;

TEST(BuyInPolicyTest, RejectsBelowMinimum) {
  BuyInRange range{50, 200};
  auto err = ValidateBuyInAmount(25, range);
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(*err, "buy_in_below_minimum");
}

TEST(BuyInPolicyTest, RejectsAboveMaximum) {
  BuyInRange range{50, 200};
  auto err = ValidateBuyInAmount(250, range);
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(*err, "buy_in_above_maximum");
}

TEST(BuyInPolicyTest, GameServerExposesTableLimits) {
  GameServer game;
  const std::string table_id = game.CreateTable("limits_table", 6, 1, 2);
  ASSERT_FALSE(table_id.empty());
  double min_buy = 0;
  double max_buy = 0;
  ASSERT_TRUE(game.GetTableBuyInLimits(table_id, min_buy, max_buy));
  EXPECT_DOUBLE_EQ(min_buy, 10.0);
  EXPECT_DOUBLE_EQ(max_buy, 200.0);
  auto err = ValidateBuyInAmount(5, BuyInRange{min_buy, max_buy});
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(*err, "buy_in_below_minimum");
}
