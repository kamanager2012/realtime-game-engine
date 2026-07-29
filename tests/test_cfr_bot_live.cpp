#include <gtest/gtest.h>

#include "poker_engine/network/ai_engine.h"
#include "poker_engine/network/cfr_policy_store.h"

using namespace poker_engine::network;

TEST(CfrBotLiveTest, StoreInitiallyEmpty) {
  EXPECT_FALSE(CfrPolicyStore::Instance().IsLoaded());
}

TEST(CfrBotLiveTest, RuleBasedDefault) {
  AIConfig cfg;
  EXPECT_EQ(cfg.strategy, AIStrategyType::RuleBased);
}
