#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::equity;
using namespace poker_engine::range;

TEST(EquityTest, KnownPreflopMatchups) {
  std::mt19937 rng(42);
  uint8_t board[1] = {0};
  EquityResult r = EquityCalculator::CalculateMonteCarlo(
      Range::FromString("AA"), Range::FromString("KK"), board, 0, 500000, rng);
  EXPECT_GT(r.equity[0], 0.80f);
  EXPECT_LT(r.equity[1], 0.20f);
  std::cout << "AA vs KK: " << r.ToString() << "\n";
}

TEST(EquityTest, CoinFlip) {
  std::mt19937 rng(42);
  uint8_t board[1] = {0};
  EquityResult r = EquityCalculator::CalculateMonteCarlo(
      Range::FromString("AKs"), Range::FromString("55"), board, 0, 500000, rng);
  EXPECT_NEAR(r.equity[0], 0.45f, 0.08f);
  std::cout << "AKs vs 55: " << r.ToString() << "\n";
}

TEST(EquityTest, FloppedNuts) {
  std::mt19937 rng(42);
  uint8_t board[3] = {Card::Parse("Th").Id(), Card::Parse("Jh").Id(), Card::Parse("Qd").Id()};
  EquityResult r = EquityCalculator::CalculateMonteCarlo(
      Range::FromString("AKs"), Range::FullCombinatorial(), board, 3, 50000, rng);
  EXPECT_GT(r.equity[0], 0.90f);
  std::cout << "AKs (Broadway straight) vs random on QJT: " << r.ToString() << "\n";
}

TEST(EquityTest, TiedAtShowdown) {
  std::mt19937 rng(42);
  uint8_t board[3] = {Card::Parse("Kc").Id(), Card::Parse("7d").Id(), Card::Parse("2h").Id()};
  EquityResult r = EquityCalculator::CalculateMonteCarlo(
      Range::FromString("AA"), Range::FromString("AA"), board, 3, 100000, rng);
  EXPECT_NEAR(r.equity[0], 0.50f, 0.03f);
  std::cout << "AA vs AA on K72: " << r.ToString() << "\n";
}

TEST(EquityTest, FullBoardEvaluation) {
  std::mt19937 rng(42);
  uint8_t board[5] = {Card::Parse("Ks").Id(), Card::Parse("7h").Id(), Card::Parse("2d").Id(),
                      Card::Parse("3c").Id(), Card::Parse("9s").Id()};
  EquityResult r = EquityCalculator::CalculateMonteCarlo(
      Range::FromString("AA"), Range::FromString("QQ"), board, 5, 10000, rng);
  EXPECT_GT(r.equity[0], 0.99f);
  std::cout << "AhKh vs QcQd on K7239: " << r.ToString() << "\n";
}

// CalculateExact on a full 5-card board must be deterministic and exact:
// AA is the nutted hand vs QQ here, so P1 wins outright.
TEST(EquityTest, ExactOnRiverIsDeterministicAndCorrect) {
  uint8_t board[5] = {Card::Parse("Ks").Id(), Card::Parse("7h").Id(), Card::Parse("2d").Id(),
                      Card::Parse("3c").Id(), Card::Parse("9s").Id()};
  EquityResult a = EquityCalculator::CalculateExact(
      Range::FromString("AA"), Range::FromString("QQ"), board, 5);
  EquityResult b = EquityCalculator::CalculateExact(
      Range::FromString("AA"), Range::FromString("QQ"), board, 5);
  // Deterministic: identical inputs -> identical outputs.
  EXPECT_EQ(a.equity[0], b.equity[0]);
  EXPECT_EQ(a.equity[1], b.equity[1]);
  // Exact: AA beats QQ on this board with no possible improvement.
  EXPECT_NEAR(a.equity[0], 1.0f, 1e-6f);
  EXPECT_NEAR(a.equity[1], 0.0f, 1e-6f);
  std::cout << "Exact AA vs QQ on K7239: " << a.ToString() << "\n";
}

// CalculateExact supports pre-flop / flop / turn via board completion.
// Cross-check the exact result against a high-sample Monte Carlo.
TEST(EquityTest, ExactFlopMatchesMonteCarlo) {
  std::mt19937 rng(7);
  uint8_t flop[3] = {Card::Parse("Th").Id(), Card::Parse("Jh").Id(), Card::Parse("Qd").Id()};
  EquityResult exact = EquityCalculator::CalculateExact(
      Range::FromString("AKs"), Range::FromString("55"), flop, 3);
  EquityResult mc = EquityCalculator::CalculateMonteCarlo(
      Range::FromString("AKs"), Range::FromString("55"), flop, 3, 400000, rng);
  EXPECT_NEAR(exact.equity[0], mc.equity[0], 0.02f)
      << "Exact flop equity should match MC within sampling noise";
  std::cout << "Exact AKs vs 55 on QJT: " << exact.ToString() << "\n";
  std::cout << "MC   AKs vs 55 on QJT: " << mc.ToString() << "\n";
}

// Two identical exact calls on a turn board must agree (determinism).
TEST(EquityTest, ExactTurnIsDeterministic) {
  uint8_t turn[4] = {Card::Parse("Ks").Id(), Card::Parse("7h").Id(),
                      Card::Parse("2d").Id(), Card::Parse("3c").Id()};
  EquityResult a = EquityCalculator::CalculateExact(
      Range::FromString("AA"), Range::FromString("KK"), turn, 4);
  EquityResult b = EquityCalculator::CalculateExact(
      Range::FromString("AA"), Range::FromString("KK"), turn, 4);
  EXPECT_EQ(a.equity[0], b.equity[0]);
  EXPECT_EQ(a.equity[1], b.equity[1]);
  std::cout << "Exact AA vs KK on K723 turn: " << a.ToString() << "\n";
}
