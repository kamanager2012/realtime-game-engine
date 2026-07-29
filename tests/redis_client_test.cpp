#include <gtest/gtest.h>

#include "poker_engine/network/redis_client.h"

using namespace poker_engine::network;

TEST(RedisClientTest, ParseUrlHostPort) {
  RedisConfig cfg;
  ASSERT_TRUE(RedisClient::ParseUrl("redis://127.0.0.1:6380", cfg));
  EXPECT_EQ(cfg.host, "127.0.0.1");
  EXPECT_EQ(cfg.port, 6380);
}

TEST(RedisClientTest, ParseBareHost) {
  RedisConfig cfg;
  ASSERT_TRUE(RedisClient::ParseUrl("redis", cfg));
  EXPECT_EQ(cfg.host, "redis");
  EXPECT_EQ(cfg.port, 6379);
}
