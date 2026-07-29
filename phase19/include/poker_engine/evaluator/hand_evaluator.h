#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace poker_engine::evaluator {

enum class HandRank : uint16_t {
  HighCard = 0,
  OnePair = 1,
  TwoPair = 2,
  ThreeOfKind = 3,
  Straight = 4,
  Flush = 5,
  FullHouse = 6,
  FourOfKind = 7,
  StraightFlush = 8,
  RoyalFlush = 9,
};

struct HandResult {
  HandRank rank;
  uint16_t tiebreaker;
  int strength;  // 0-7462
};

class HandEvaluator {
 public:
  HandEvaluator();

  // 评估 5-7 张牌
  HandResult Evaluate(const uint8_t cards[], int num_cards) const;

  // 比较两手牌
  int CompareTwoHands(uint8_t h1_c1, uint8_t h1_c2, uint8_t h2_c1, uint8_t h2_c2,
                      const uint8_t community[5], int comm_count) const;

  // 快速评估胜率（蒙特卡洛）
  double GetEquity(uint8_t c1, uint8_t c2, const uint8_t community[5], int comm_count,
                   int num_opponents = 1) const;

  static const char* HandRankName(HandRank rank);

  // 两张牌的哈希（169 个 bucket）
  uint16_t TwoCardHash(uint8_t c1, uint8_t c2) const;

 private:
  std::array<uint16_t, 52 * 52> two_card_rank_;

  static int RankOf(uint8_t card) { return card % 13; }
  static int SuitOf(uint8_t card) { return card / 13; }

  HandResult EvaluateSevenCards(const uint8_t cards[7]) const;

  void PrecomputeTwoCardRanks();
};

}  // namespace poker_engine::evaluator
