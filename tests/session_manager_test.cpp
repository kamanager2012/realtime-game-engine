#include "poker_engine/network/session_manager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace poker_engine::network;

class SessionManagerTest : public ::testing::Test {
 protected:
  SessionManager sessions_;

  SessionManagerTest() : sessions_(std::chrono::seconds(60)) {}
};

TEST_F(SessionManagerTest, CreateSessionReturnsNonEmptyToken) {
  std::string token = sessions_.Create(100, "table_1");
  EXPECT_FALSE(token.empty());
  EXPECT_GT(token.size(), 10u);
}

TEST_F(SessionManagerTest, CreatedSessionCanBeRetrieved) {
  std::string token = sessions_.Create(100, "table_1");
  auto session = sessions_.Get(token);

  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(session->player_id, 100);
  EXPECT_EQ(session->table_id, "table_1");
  EXPECT_EQ(session->token, token);
}

TEST_F(SessionManagerTest, InvalidTokenReturnsEmpty) {
  auto session = sessions_.Get("invalid_token");
  EXPECT_FALSE(session.has_value());
}

TEST_F(SessionManagerTest, ConnectMarksSessionConnected) {
  std::string token = sessions_.Create(100, "table_1");
  EXPECT_FALSE(sessions_.Get(token)->is_connected);

  bool connected = sessions_.Connect(token);
  EXPECT_TRUE(connected);
  EXPECT_TRUE(sessions_.Get(token)->is_connected);
}

TEST_F(SessionManagerTest, DisconnectMarksSessionDisconnected) {
  std::string token = sessions_.Create(100, "table_1");
  sessions_.Connect(token);

  sessions_.Disconnect(token);
  EXPECT_FALSE(sessions_.Get(token)->is_connected);
}

TEST_F(SessionManagerTest, ReconnectWorksWithinTimeout) {
  std::string token = sessions_.Create(100, "table_1");
  sessions_.Connect(token);
  sessions_.Disconnect(token);

  bool reconnected = sessions_.Reconnect(token);
  EXPECT_TRUE(reconnected);
  EXPECT_TRUE(sessions_.Get(token)->is_connected);
}

TEST_F(SessionManagerTest, IsAuthorizedValidatesSessionAndTable) {
  std::string token = sessions_.Create(100, "table_1");
  sessions_.Connect(token);

  EXPECT_TRUE(sessions_.IsAuthorized(token, "table_1"));
  EXPECT_FALSE(sessions_.IsAuthorized(token, "wrong_table"));
  EXPECT_FALSE(sessions_.IsAuthorized("wrong_token", "table_1"));
}

TEST_F(SessionManagerTest, ActiveSessionsNotCleanedUp) {
  std::string token = sessions_.Create(100, "table_1");
  sessions_.Connect(token);

  int cleaned = sessions_.CleanupExpired();
  EXPECT_EQ(cleaned, 0);

  auto session = sessions_.Get(token);
  ASSERT_TRUE(session.has_value());
  EXPECT_TRUE(session->is_connected);
}

TEST_F(SessionManagerTest, MultipleSessionsIndependent) {
  std::string t1 = sessions_.Create(100, "table_1");
  std::string t2 = sessions_.Create(200, "table_2");
  std::string t3 = sessions_.Create(300, "table_3");

  sessions_.Connect(t1);
  sessions_.Connect(t3);

  EXPECT_TRUE(sessions_.IsAuthorized(t1, "table_1"));
  EXPECT_FALSE(sessions_.IsAuthorized(t1, "table_2"));
  EXPECT_TRUE(sessions_.IsAuthorized(t3, "table_3"));
  EXPECT_FALSE(sessions_.IsAuthorized(t2, "table_2"));  // t2 未连接
}
