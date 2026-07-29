#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/game/action.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/network/ai_engine.h"
#include "poker_engine/network/game_server.h"
#include "poker_engine/network/auth_service.h"

using namespace poker_engine::network;
using namespace poker_engine::game;

TEST(Phase13Test, AITrivial) { EXPECT_TRUE(true); }
TEST(Phase13Test, Placeholder) { EXPECT_TRUE(true); }

TEST(Phase13Test, AuthLockoutTriggersAfterMaxAttempts) {
  AuthService auth("test-secret");
  ASSERT_TRUE(auth.Register("lockuser", "correctpw", "Lock User").success);

  // 5 次错误密码 -> 第 6 次（即便正确）应被锁定
  for (int i = 0; i < 5; ++i) {
    auto r = auth.Login("lockuser", "wrongpw");
    EXPECT_FALSE(r.success);
  }
  auto locked = auth.Login("lockuser", "correctpw");
  EXPECT_FALSE(locked.success);
  EXPECT_NE(locked.error_message.find("locked"), std::string::npos);

  // 锁定窗口内，正确密码仍被拒
  auto still_locked = auth.Login("lockuser", "correctpw");
  EXPECT_FALSE(still_locked.success);
}

TEST(Phase13Test, AuthSuccessClearsAttemptsBeforeLock) {
  AuthService auth("test-secret");
  ASSERT_TRUE(auth.Register("clearuser", "pw12345", "Clear").success);

  auth.Login("clearuser", "bad1");
  auth.Login("clearuser", "bad2");
  EXPECT_TRUE(auth.Login("clearuser", "pw12345").success);

  // 清零后再次错误不应锁定
  EXPECT_FALSE(auth.Login("clearuser", "bad3").success);
  // 正确密码仍可用
  EXPECT_TRUE(auth.Login("clearuser", "pw12345").success);
}

TEST(Phase13Test, GameServerAdapterCreatesTablesAndStateJson) {
  ServerConfig config;
  config.max_tables = 4;
  GameServer server(config);

  int broadcasts = 0;
  std::string last_table;
  std::string last_payload;
  server.SetBroadcastCallback([&](const std::string& table_id, const std::string& payload) {
    ++broadcasts;
    last_table = table_id;
    last_payload = payload;
  });

  EXPECT_EQ(server.CreateTable("main", 6, 1, 2), "main");
  EXPECT_EQ(server.CreateTable("main", 6, 1, 2), "main");
  EXPECT_TRUE(server.JoinTable(101, "main", "Alice", 0, 200, "token"));
  EXPECT_EQ(server.AddBots("main", 2, 200), 2);

  std::string state = server.GetTableStateJSON("main");
  EXPECT_NE(state.find("\"table_id\":\"main\""), std::string::npos);
  EXPECT_NE(state.find("\"display_name\":\"Alice\""), std::string::npos);
  EXPECT_NE(state.find("\"current_player_id\""), std::string::npos);
  EXPECT_NE(state.find("\"community_cards\""), std::string::npos);
  EXPECT_NE(state.find("\"players\""), std::string::npos);

  std::string tables = server.GetTablesListJSON();
  EXPECT_NE(tables.find("\"id\":\"main\""), std::string::npos);
  EXPECT_NE(tables.find("\"occupied\":3"), std::string::npos);

  EXPECT_GT(broadcasts, 0);
  EXPECT_EQ(last_table, "main");
  EXPECT_NE(last_payload.find("\"table_id\":\"main\""), std::string::npos);
}

TEST(Phase13Test, GameServerAdapterHonorsRequestedSeat) {
  ServerConfig config;
  config.max_tables = 2;
  GameServer server(config);
  ASSERT_EQ(server.CreateTable("main", 6, 1, 2), "main");

  ASSERT_TRUE(server.JoinTable(101, "main", "Alice", 4, 200, "token"));
  EXPECT_FALSE(server.JoinTable(102, "main", "Bob", 4, 200, "token"));
  EXPECT_TRUE(server.LeaveTable(101, "main"));
  EXPECT_TRUE(server.JoinTable(102, "main", "Bob", 4, 200, "token"));

  std::string state = server.GetTableStateJSON("main");
  EXPECT_EQ(state.find("\"player_id\":101"), std::string::npos);
  EXPECT_NE(state.find("\"player_id\":102"), std::string::npos);
  EXPECT_NE(state.find("\"seat_index\":4"), std::string::npos);
}

TEST(Phase13Test, GameServerAdapterStartsAndAdvancesBotTurns) {
  ServerConfig config;
  config.max_tables = 2;
  GameServer server(config);
  ASSERT_EQ(server.CreateTable("main", 6, 1, 2), "main");
  ASSERT_TRUE(server.JoinTable(101, "main", "Alice", 0, 200, "token"));
  ASSERT_EQ(server.AddBots("main", 2, 200), 2);

  ASSERT_TRUE(server.StartGame("main"));
  server.ProcessBotActions("main");

  std::string state = server.GetTableStateJSON("main");
  EXPECT_NE(state.find("\"status\":\"playing\""), std::string::npos);
  EXPECT_NE(state.find("\"phase\":\"preflop\""), std::string::npos);
  EXPECT_NE(state.find("\"pot\":"), std::string::npos);
}

TEST(Phase13Test, HoleCardsHiddenFromOtherViewers) {
  ServerConfig config;
  config.max_tables = 2;
  GameServer server(config);
  ASSERT_EQ(server.CreateTable("main", 6, 1, 2), "main");
  ASSERT_TRUE(server.JoinTable(101, "main", "Alice", 0, 200, "token"));
  ASSERT_EQ(server.AddBots("main", 2, 200), 2);
  ASSERT_TRUE(server.StartGame("main"));

  std::string alice_view = server.GetTableStateJSON("main", 101);
  std::string spectator_view = server.GetTableStateJSON("main", 0);

  auto alice_pos = alice_view.find("\"player_id\":101");
  ASSERT_NE(alice_pos, std::string::npos);
  auto alice_hole = alice_view.find("\"hole_cards\":[", alice_pos);
  ASSERT_NE(alice_hole, std::string::npos);
  auto alice_hole_end = alice_view.find("]", alice_hole);
  EXPECT_NE(alice_view.substr(alice_hole, alice_hole_end - alice_hole + 1), "\"hole_cards\":[]");

  auto bot_pos = spectator_view.find("\"player_id\":-1");
  ASSERT_NE(bot_pos, std::string::npos);
  auto bot_hole = spectator_view.find("\"hole_cards\":[", bot_pos);
  ASSERT_NE(bot_hole, std::string::npos);
  auto bot_hole_end = spectator_view.find("]", bot_hole);
  EXPECT_EQ(spectator_view.substr(bot_hole, bot_hole_end - bot_hole + 1), "\"hole_cards\":[]");
}

