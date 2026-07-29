#include <gtest/gtest.h>

#include "poker_engine/network/distributed_session_store.h"

using namespace poker_engine::network;

TEST(DistributedSessionStoreTest, LocalRevocationWithoutRedis) {
  DistributedSessionStore store("test-node");
  EXPECT_FALSE(store.IsRedisConnected());

  const std::string token = "jwt.token.value";
  EXPECT_FALSE(store.IsRevoked(token));
  store.Revoke(token, 60);
  EXPECT_TRUE(store.IsRevoked(token));
}

TEST(DistributedSessionStoreTest, InstanceIdExposed) {
  DistributedSessionStore store("node-a");
  EXPECT_EQ(store.instance_id(), "node-a");
}
