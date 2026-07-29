#include <gtest/gtest.h>

#include "poker_engine/replay/replay_engine.h"
#include "poker_engine/replay/replay_types.h"

using namespace poker_engine::replay;

TEST(ReplayIntegrationTest, ReplayEventSerialization) {
  ReplayEvent evt;
  evt.sequence_id = 42;
  evt.type = ReplayEventType::ActionTaken;
  evt.hand_id = 1;
  evt.player_id = 100;
  evt.timestamp = 1.5;
  evt.details = R"({"action":"call","amount":10})";
  auto serialized = evt.Serialize();
  auto deserialized = ReplayEvent::Deserialize(serialized);
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->sequence_id, 42);
  EXPECT_EQ(deserialized->type, ReplayEventType::ActionTaken);
  EXPECT_EQ(deserialized->player_id, 100);
  EXPECT_NEAR(deserialized->timestamp, 1.5, 0.001);
}

TEST(ReplayIntegrationTest, ReplayQuerySQL) {
  ReplayQuery q;
  q.player_id = 42;
  q.hand_id = 100;
  auto sql = q.ToSQLWhere();
  EXPECT_NE(sql.find("player_id"), std::string::npos);
  EXPECT_NE(sql.find("hand_id"), std::string::npos);
}

TEST(ReplayIntegrationTest, ReplayQueryEmpty) {
  ReplayQuery q;
  auto sql = q.ToSQLWhere();
  EXPECT_TRUE(sql.empty());
}

TEST(ReplayIntegrationTest, HandSnapshotDefault) {
  HandSnapshot snap;
  EXPECT_EQ(snap.hand_id, 0);
  EXPECT_TRUE(snap.players.empty());
  EXPECT_TRUE(snap.community_cards.empty());
}
