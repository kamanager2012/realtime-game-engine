#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <iostream>

#include "poker_engine/cfr/cfr_engine.h"
#include "poker_engine/cfr/compact_node.h"
#include "poker_engine/cfr/disk_backed_store.h"
#include "poker_engine/cfr/types.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/evaluator/hand_evaluator.h"
#include "poker_engine/game/game_state.h"

using namespace poker_engine::cfr;
using namespace poker_engine::evaluator;

class PerformanceTest : public ::testing::Test {
 protected:
  void TearDown() override { printf("All performance tests completed\n"); }
};

// ==================== QuickEval 性能测试 ====================

TEST_F(PerformanceTest, QuickEval_Perf) {
  HandEvaluator eval;
  uint8_t cards[5] = {0, 13, 26, 39, 1};  // A♠, A♥, A♦, A♣, K♠

  const int iterations = 500000;
  auto start = std::chrono::steady_clock::now();

  uint32_t sum = 0;
  for (int i = 0; i < iterations; i++) {
    cards[0] = static_cast<uint8_t>(i % 52);
    sum += eval.Evaluate(cards, 5).strength;
  }

  auto elapsed = std::chrono::steady_clock::now() - start;
  double us = std::chrono::duration<double, std::micro>(elapsed).count();

  printf("\n==================================================\n");
  printf(" QuickEval 5-card perf:\n");
  printf("   %d evaluations in %.2f us (total)\n", iterations, us);
  printf("   Avg: %.2f ns/op\n", (us * 1000.0) / iterations);
  printf("   Throughput: %.0f evals/sec\n", iterations / (us / 1000000.0));
  printf("   Checksum: %u\n", sum);
  printf("==================================================\n\n");

  EXPECT_LT(us / iterations, 0.5);
}

// ==================== 7 张牌评估性能 ====================

TEST_F(PerformanceTest, SevenCardEval_Perf) {
  HandEvaluator eval;
  uint8_t cards[7] = {0, 13, 26, 39, 1, 14, 27};

  const int iterations = 200000;
  auto start = std::chrono::steady_clock::now();

  uint32_t sum = 0;
  for (int i = 0; i < iterations; i++) {
    cards[0] = static_cast<uint8_t>(i % 52);
    sum += eval.Evaluate(cards, 7).strength;
  }

  auto elapsed = std::chrono::steady_clock::now() - start;
  double ms = std::chrono::duration<double, std::milli>(elapsed).count();

  printf("\n[Perf] 7-card eval: %d evals in %.0fms = %.0f evals/sec\n", iterations, ms,
         iterations / (ms / 1000.0));

  EXPECT_GT(iterations / (ms / 1000.0), 100000);
}

// ==================== 两手牌比较性能 ====================

TEST_F(PerformanceTest, TwoHandCompare_Perf) {
  HandEvaluator eval;
  uint8_t community[5] = {2, 15, 28, 41, 5};

  const int iterations = 500000;
  auto start = std::chrono::steady_clock::now();

  int result_sum = 0;
  for (int i = 0; i < iterations; i++) {
    uint8_t c1 = i % 52;
    uint8_t c2 = (i + 1) % 52;
    uint8_t c3 = (i + 2) % 52;
    uint8_t c4 = (i + 3) % 52;
    result_sum += eval.CompareTwoHands(c1, c2, c3, c4, community, 5);
  }

  auto elapsed = std::chrono::steady_clock::now() - start;
  double ns = std::chrono::duration<double, std::nano>(elapsed).count() / iterations;

  printf("\n[Perf] Two-hand compare: %.1f ns/op (%d total)\n", ns, iterations);

  EXPECT_LT(ns, 2000);
}

// ==================== CFR 节点存储性能 ====================

TEST_F(PerformanceTest, CompactNodeStore_Perf) {
  CompactNodeStore store(100000);

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < 100000; i++) {
    uint64_t key = static_cast<uint64_t>(i) * 2654435761ULL;
    auto* node = store.GetOrCreate(key);
    if (node) {
      node->AddRegret(0, 1.0);
      node->SetStrategy(0, 0.5);
      node->IncrementVisits();
    }
  }

  auto elapsed = std::chrono::steady_clock::now() - start;
  double us = std::chrono::duration<double, std::micro>(elapsed).count();

  printf("\n[Perf] CompactNodeStore 100K nodes: %.0f us (%.0f ns/op)\n", us, us * 1000.0 / 100000);
  printf("       Memory: %zu KB\n", store.MemoryBytes() / 1024);

  EXPECT_LT(us / 100000, 0.2);
}

// ==================== CFR 节点磁盘存储性能 ====================

TEST_F(PerformanceTest, DiskBackedStore_Perf) {
  DiskBackedNodeStoreConfig config;
  config.max_memory_nodes = 10000;
  config.memory_budget_mb = 1;
  config.disk_path = "/tmp/poker_test/cfr_nodes.dat";

  DiskBackedNodeStore store(config);

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < 50000; i++) {
    uint64_t key = static_cast<uint64_t>(i) * 2654435761ULL;
    auto* node = store.GetOrCreateNode(key);
    if (node) {
      node->AddRegret(0, 0.5);
      node->IncrementVisits();
    }
  }

  auto elapsed =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

  printf("\n[Perf] DiskBackedStore 50K nodes: %.0fms\n", elapsed);
  printf("       Memory nodes: %zu, Disk nodes: %zu\n", store.MemoryNodes(), store.DiskNodes());

  EXPECT_TRUE(store.SaveToDisk("/tmp/poker_test/cfr_save.dat"));
  EXPECT_TRUE(store.LoadFromDisk("/tmp/poker_test/cfr_save.dat"));

  EXPECT_GT(elapsed, 0);
}

// ==================== 全模拟基准（100 手牌）========================

TEST_F(PerformanceTest, FullSimulation_Benchmark) {
  const int NUM_HANDS = 100;

  HandEvaluator eval;

  auto start = std::chrono::steady_clock::now();

  for (int hand = 0; hand < NUM_HANDS; ++hand) {
    uint8_t next_card = (hand * 9) % 52;

    // 6 个玩家，每人 2 张手牌
    std::vector<std::array<uint8_t, 2>> hole_cards(6);
    for (int i = 0; i < 6; ++i) {
      hole_cards[i] = {next_card++, next_card++};
    }

    // 5 张公共牌
    std::vector<uint8_t> community;
    for (int c = 0; c < 5; ++c) community.push_back(next_card++);

    // 摊牌评估
    int best_strength = -1;
    int winner_id = -1;
    for (int i = 0; i < 6; ++i) {
      uint8_t all_cards[7];
      all_cards[0] = hole_cards[i][0];
      all_cards[1] = hole_cards[i][1];
      std::copy(community.begin(), community.end(), all_cards + 2);

      auto result = eval.Evaluate(all_cards, 7);
      if (result.strength > best_strength) {
        best_strength = result.strength;
        winner_id = 100 + i;
      }
    }
    EXPECT_NE(winner_id, -1);
  }

  auto elapsed = std::chrono::steady_clock::now() - start;
  double ms = std::chrono::duration<double, std::milli>(elapsed).count();

  printf("\n[Perf] Full simulation: %d hands in %.0fms (%.1f hands/sec)\n", NUM_HANDS, ms,
         NUM_HANDS / (ms / 1000.0));

  EXPECT_LT(ms / NUM_HANDS, 1.0);
}
