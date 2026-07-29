#include "poker_engine/evaluator/hand_evaluator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

using namespace poker_engine::evaluator;

class HandEvaluatorTest : public ::testing::Test {
 protected:
  HandEvaluator evaluator_;
};

// ==================== 牌编码辅助 ====================

// 牌编码: index = rank + suit * 13
// rank: 0=Ace, 1=2, 2=3, ... 12=King
// suit: 0=♠, 1=♥, 2=♦, 3=♣

inline uint8_t MakeCard(int rank, int suit) { return static_cast<uint8_t>(rank + suit * 13); }

// ==================== 皇家同花顺 ====================

TEST_F(HandEvaluatorTest, RoyalFlush) {
  // A♠ K♠ Q♠ J♠ 10♠ + 任意
  uint8_t cards[] = {
      MakeCard(0, 0),   // A♠
      MakeCard(12, 0),  // K♠
      MakeCard(11, 0),  // Q♠
      MakeCard(10, 0),  // J♠
      MakeCard(9, 0),   // 10♠
  };

  auto result = evaluator_.Evaluate(cards, 5);
  EXPECT_EQ(result.rank, HandRank::RoyalFlush);
}

TEST_F(HandEvaluatorTest, StraightFlush) {
  // 9♥ 8♥ 7♥ 6♥ 5♥
  uint8_t cards[] = {
      MakeCard(8, 1), MakeCard(7, 1), MakeCard(6, 1), MakeCard(5, 1), MakeCard(4, 1),
  };

  auto result = evaluator_.Evaluate(cards, 5);
  EXPECT_EQ(result.rank, HandRank::StraightFlush);
}

TEST_F(HandEvaluatorTest, FourOfAKind) {
  // 4♠ 4♥ 4♦ 4♣ + K♠
  uint8_t cards[] = {
      MakeCard(3, 0), MakeCard(3, 1), MakeCard(3, 2), MakeCard(3, 3), MakeCard(12, 0),
  };

  auto result = evaluator_.Evaluate(cards, 5);
  EXPECT_EQ(result.rank, HandRank::FourOfKind);
}

TEST_F(HandEvaluatorTest, FullHouse) {
  // Q♠ Q♥ Q♦ 7♣ 7♥
  uint8_t cards[] = {
      MakeCard(11, 0), MakeCard(11, 1), MakeCard(11, 2), MakeCard(6, 3), MakeCard(6, 1),
  };

  auto result = evaluator_.Evaluate(cards, 5);
  EXPECT_EQ(result.rank, HandRank::FullHouse);
}

TEST_F(HandEvaluatorTest, Flush) {
  // 全是黑桃但不成顺子
  uint8_t cards[] = {
      MakeCard(0, 0), MakeCard(5, 0), MakeCard(8, 0), MakeCard(11, 0), MakeCard(3, 0),
  };

  auto result = evaluator_.Evaluate(cards, 5);
  EXPECT_EQ(result.rank, HandRank::Flush);
}

TEST_F(HandEvaluatorTest, Straight) {
  // 不同花色顺子: 5♥ 4♠ 3♦ 2♣ A♥
  uint8_t cards[] = {
      MakeCard(4, 1), MakeCard(3, 0), MakeCard(2, 2), MakeCard(1, 3), MakeCard(0, 1),
  };

  auto result = evaluator_.Evaluate(cards, 5);
  EXPECT_EQ(result.rank, HandRank::Straight);
}

TEST_F(HandEvaluatorTest, WheelStraight) {
  // A低顺: A♠ 2♥ 3♦ 4♣ 5♥
  uint8_t cards[] = {
      MakeCard(0, 0), MakeCard(1, 1), MakeCard(2, 2), MakeCard(3, 3), MakeCard(4, 1),
  };

  auto result = evaluator_.Evaluate(cards, 5);
  EXPECT_EQ(result.rank, HandRank::Straight);
}

TEST_F(HandEvaluatorTest, ThreeOfAKind) {
  // 8♠ 8♥ 8♦ K♣ 3♥
  uint8_t cards[] = {
      MakeCard(7, 0), MakeCard(7, 1), MakeCard(7, 2), MakeCard(12, 3), MakeCard(2, 1),
  };

  auto result = evaluator_.Evaluate(cards, 5);
  EXPECT_EQ(result.rank, HandRank::ThreeOfKind);
}

TEST_F(HandEvaluatorTest, TwoPair) {
  // J♠ J♥ 4♦ 4♣ 9♥
  uint8_t cards[] = {
      MakeCard(10, 0), MakeCard(10, 1), MakeCard(3, 2), MakeCard(3, 3), MakeCard(8, 1),
  };

  auto result = evaluator_.Evaluate(cards, 5);
  EXPECT_EQ(result.rank, HandRank::TwoPair);
}

TEST_F(HandEvaluatorTest, OnePair) {
  // 6♠ 6♥ K♦ 9♣ 3♥
  uint8_t cards[] = {
      MakeCard(5, 0), MakeCard(5, 1), MakeCard(12, 2), MakeCard(8, 3), MakeCard(2, 1),
  };

  auto result = evaluator_.Evaluate(cards, 5);
  EXPECT_EQ(result.rank, HandRank::OnePair);
}

TEST_F(HandEvaluatorTest, HighCard) {
  // 散牌: A♠ K♥ Q♦ J♣ 8♥
  uint8_t cards[] = {
      MakeCard(0, 0), MakeCard(12, 1), MakeCard(11, 2), MakeCard(10, 3), MakeCard(7, 1),
  };

  auto result = evaluator_.Evaluate(cards, 5);
  EXPECT_EQ(result.rank, HandRank::HighCard);
}

// ==================== 7 张牌评估 ====================

TEST_F(HandEvaluatorTest, SevenCardBestHand) {
  // 7 张牌，最佳 5 张应组成同花
  // K♥ Q♥ J♥ 10♥ 7♥ 3♠ 2♦ → K-high Flush in hearts
  uint8_t cards[] = {
      MakeCard(12, 1), MakeCard(11, 1), MakeCard(10, 1), MakeCard(9, 1),
      MakeCard(6, 1),  MakeCard(2, 0),  MakeCard(1, 2),
  };

  auto result = evaluator_.Evaluate(cards, 7);
  EXPECT_EQ(result.rank, HandRank::Flush);
  EXPECT_GT(result.strength, 0);
}

// ==================== 手牌比较 ====================

TEST_F(HandEvaluatorTest, CompareTwoHands) {
  // 一对 vs 高牌 → 一对获胜
  uint8_t community[5] = {
      MakeCard(2, 0), MakeCard(6, 1), MakeCard(9, 2), MakeCard(12, 3), MakeCard(4, 1),
  };

  // 玩家 1: A♦ A♣ (一对A)
  // 玩家 2: K♠ Q♦ (高牌)
  int result = evaluator_.CompareTwoHands(MakeCard(0, 2), MakeCard(0, 3),    // AA
                                          MakeCard(12, 0), MakeCard(11, 2),  // KQ
                                          community, 5);

  EXPECT_GT(result, 0);  // 玩家 1 获胜
}

TEST_F(HandEvaluatorTest, TieResult) {
  // 完全相同的两手牌 → 平局
  uint8_t community[5] = {
      MakeCard(2, 0), MakeCard(6, 1), MakeCard(9, 2), MakeCard(12, 3), MakeCard(4, 1),
  };

  // 双方都用同样的手牌
  int result = evaluator_.CompareTwoHands(MakeCard(0, 0), MakeCard(1, 1), MakeCard(0, 2),
                                          MakeCard(1, 3), community, 5);

  EXPECT_EQ(result, 0);  // 平局
}

// ==================== Equity 计算测试 ====================

TEST_F(HandEvaluatorTest, EquityVsRandom) {
  // AA vs 随机对手应 > 50% 胜率
  uint8_t community[5] = {0, 0, 0, 0, 0};  // 无公共牌

  double equity = evaluator_.GetEquity(MakeCard(0, 0), MakeCard(0, 1),  // AA
                                       community, 0,
                                       1);  // 1 个对手

  EXPECT_GT(equity, 0.5);  // AA vs 随机应领先
}

TEST_F(HandEvaluatorTest, EquityVsBetterHand) {
  // 72o vs AA 应较低
  uint8_t community[5] = {0, 0, 0, 0, 0};

  double equity = evaluator_.GetEquity(MakeCard(6, 2), MakeCard(5, 3),  // 7♣ 2♦
                                       community, 0, 1);

  EXPECT_LT(equity, 0.5);  // 弱牌应低于50%
}

// ==================== TwoCardHash 测试 ====================

TEST_F(HandEvaluatorTest, TwoCardHash_SUEM) {
  // 同花
  uint16_t suited = evaluator_.TwoCardHash(MakeCard(0, 0), MakeCard(12, 0));   // A♠ K♠
  uint16_t offsuit = evaluator_.TwoCardHash(MakeCard(0, 0), MakeCard(12, 1));  // A♠ K♥

  EXPECT_NE(suited, offsuit);

  // 同点数对子
  uint16_t pair = evaluator_.TwoCardHash(MakeCard(0, 0), MakeCard(0, 1));  // AA
  EXPECT_NE(pair, suited);
}

// ==================== 评估器性能测试 ====================

TEST_F(HandEvaluatorTest, EvaluatePerformance) {
  uint8_t seven_cards[7];
  for (int i = 0; i < 7; ++i) seven_cards[i] = i;

  const int iterations = 500000;

  auto start = std::chrono::high_resolution_clock::now();
  int64_t total_strength = 0;
  for (int i = 0; i < iterations; ++i) {
    auto result = evaluator_.Evaluate(seven_cards, 7);
    total_strength += result.strength;
  }
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::high_resolution_clock::now() - start)
                     .count();

  double eval_per_sec = iterations / (elapsed / 1000.0);

  printf("[Perf] HandEvaluator: %d 7-card evals in %lldms (%.0f evals/sec)\n", iterations,
         (long long)elapsed, eval_per_sec);

  // 期望: > 200K 次评估/秒
  EXPECT_GT(eval_per_sec, 200000.0);

  // 验证 volatile 使用
  EXPECT_GT(total_strength, 0);
}

// ==================== 7c4c 牌型分布验证 ====================

TEST_F(HandEvaluatorTest, HandRankDistribution) {
  // 遍历大量 5 张牌组合，验证排名分布合理
  int rank_counts[10] = {};
  int total = 0;

  // 采样（C(52,5) = 2598960 种组合中的子集）
  uint8_t cards[5];
  for (int c0 = 0; c0 < 48; c0 += 3) {
    for (int c1 = c0 + 1; c1 < 49; c1 += 3) {
      for (int c2 = c1 + 1; c2 < 50; c2 += 3) {
        for (int c3 = c2 + 1; c3 < 51; c3 += 3) {
          for (int c4 = c3 + 1; c4 < 52; c4 += 3) {
            cards[0] = c0;
            cards[1] = c1;
            cards[2] = c2;
            cards[3] = c3;
            cards[4] = c4;

            auto result = evaluator_.Evaluate(cards, 5);
            rank_counts[static_cast<int>(result.rank)]++;
            total++;
          }
        }
      }
    }
  }

  // 验证采样数
  EXPECT_GT(total, 10000);

  // 验证皇家同花顺最少
  EXPECT_LT(rank_counts[static_cast<int>(HandRank::RoyalFlush)], 5);

  // 验证高牌最多
  EXPECT_GT(rank_counts[static_cast<int>(HandRank::HighCard)],
            rank_counts[static_cast<int>(HandRank::Flush)]);

  printf("Hand rank distribution (sample of %d hands):\n", total);
  const char* names[] = {"RoyalFlush", "StraightFlush", "FourOfKind", "FullHouse", "Flush",
                         "Straight",   "ThreeOfKind",   "TwoPair",    "OnePair",   "HighCard"};
  for (int i = 0; i < 10; ++i) {
    printf("  %s: %d\n", names[i], rank_counts[i]);
  }
}
