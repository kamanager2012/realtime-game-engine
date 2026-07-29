#include "poker_engine/base/serialization.h"

#include <gtest/gtest.h>

using namespace poker_engine::base;

class SerializationTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(SerializationTest, HeartbeatRoundTrip) {
  WSMessage msg;
  msg.type = MessageType::Heartbeat;
  msg.seq = 42;
  msg.timestamp = "2026-06-15T10:30:00.000Z";
  msg.payload = "{}";

  auto serialized = msg.Serialize();
  auto parsed = WSMessage::Deserialize(serialized);

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->type, MessageType::Heartbeat);
  EXPECT_EQ(parsed->seq, 42u);
}

TEST_F(SerializationTest, JoinTableMessageBuild) {
  auto msg = MessageBuilder::JoinTable(1, "table_1", "Alice", 3, 100);
  EXPECT_EQ(msg.type, MessageType::JoinTable);
  EXPECT_EQ(msg.seq, 1u);
  EXPECT_FALSE(msg.payload.empty());

  auto roundtrip = WSMessage::Deserialize(msg.Serialize());
  ASSERT_TRUE(roundtrip.has_value());
  EXPECT_EQ(roundtrip->type, MessageType::JoinTable);
}

TEST_F(SerializationTest, PlayerActionMessageBuild) {
  auto msg = MessageBuilder::PlayerAction(2, "table_1", "player_100", "raise", 50);
  EXPECT_EQ(msg.type, MessageType::PlayerAction);

  auto serialized = msg.Serialize();
  auto parsed = WSMessage::Deserialize(serialized);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->type, MessageType::PlayerAction);
  EXPECT_EQ(parsed->seq, 2u);
  EXPECT_FALSE(parsed->payload.empty());
}

TEST_F(SerializationTest, ErrorMessageBuild) {
  auto msg = MessageBuilder::Error(5, static_cast<uint16_t>(ErrorCode::InvalidAction),
                                   "Invalid raise amount", 3);

  EXPECT_EQ(msg.type, MessageType::Error);
  EXPECT_EQ(msg.seq, 5u);
  EXPECT_NE(msg.payload.find("1001"), std::string::npos);
  EXPECT_NE(msg.payload.find("Invalid raise amount"), std::string::npos);

  auto parsed = WSMessage::Deserialize(msg.Serialize());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->type, MessageType::Error);
}

TEST_F(SerializationTest, HeartbeatAckRoundTrip) {
  auto msg = MessageBuilder::HeartbeatAck(10, "2026-06-15T10:30:01.000Z");
  auto parsed = WSMessage::Deserialize(msg.Serialize());

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->type, MessageType::HeartbeatAck);
  EXPECT_EQ(parsed->seq, 10u);
}

TEST_F(SerializationTest, GameEventMessageBuild) {
  auto msg = MessageBuilder::GameEvent(3, "phase_change", "preflop -> flop");
  EXPECT_EQ(msg.type, MessageType::GameEvent);

  auto serialized = msg.Serialize();
  EXPECT_NE(serialized.find("phase_change"), std::string::npos);
}

TEST_F(SerializationTest, SpecialCharactersInPayload) {
  auto msg = MessageBuilder::Error(1, static_cast<uint16_t>(ErrorCode::ProtocolError),
                                   "Player's \"special\" request");

  auto parsed = WSMessage::Deserialize(msg.Serialize());
  ASSERT_TRUE(parsed.has_value());
}

TEST_F(SerializationTest, HighSeqNumbers) {
  auto msg = MessageBuilder::Heartbeat(999999999999ULL);
  auto parsed = WSMessage::Deserialize(msg.Serialize());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->seq, 999999999999ULL);
}
