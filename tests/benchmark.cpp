#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <random>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::range;
using namespace poker_engine::equity;
using namespace poker_engine::evaluator;
static const int BENCH_N = 100000;

template <typename T>
void Use(T&&) {}

TEST(Benchmark, CardParsePerformance) {
  auto t0 = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < BENCH_N; i++) {
    Card c = Card::Parse("As");
    Use(c);
  }
  auto t1 = std::chrono::high_resolution_clock::now();
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  std::cout << "\nCard::Parse: " << double(us) / BENCH_N << " us/call\n";
}

TEST(Benchmark, Evaluator5Performance) {
  Card cards[5] = {Card::Parse("As"), Card::Parse("Kd"), Card::Parse("Qc"), Card::Parse("Jh"),
                   Card::Parse("Ts")};
  auto t0 = std::chrono::high_resolution_clock::now();
  uint64_t sum = 0;
  for (int i = 0; i < BENCH_N; i++) {
    auto r = Evaluator::Evaluate5(cards);
    sum += r.value();
  }
  auto t1 = std::chrono::high_resolution_clock::now();
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
  std::cout << "\nEvaluate5: " << double(ns) / BENCH_N << " ns/call (sum=" << sum << ")\n";
}

TEST(Benchmark, Evaluator7Performance) {
  Card cards[7] = {Card::Parse("As"), Card::Parse("Kd"), Card::Parse("7c"), Card::Parse("Qh"),
                   Card::Parse("Ts"), Card::Parse("3d"), Card::Parse("9s")};
  auto t0 = std::chrono::high_resolution_clock::now();
  uint64_t sum = 0;
  for (int i = 0; i < BENCH_N; i++) {
    auto r = Evaluator::Evaluate7(cards);
    sum += r.value();
  }
  auto t1 = std::chrono::high_resolution_clock::now();
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
  std::cout << "\nEvaluate7: " << double(ns) / BENCH_N << " ns/call (sum=" << sum << ")\n";
}

TEST(Benchmark, RangeParsePerformance) {
  auto t0 = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 1000; i++) {
    Range r = Range::FromString("AA,KK,QQ,AKs,AKo,JJs,TTs,99,88+");
    Use(r);
  }
  auto t1 = std::chrono::high_resolution_clock::now();
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  std::cout << "\nRange::Parse: " << us / 1000 << " us/call\n";
}

TEST(Benchmark, RangeNormalizePerformance) {
  Range r = Range::FromString("22+,A2s+,K2s+,Q2s+,J2s+,T2s+");
  auto t0 = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 10000; i++) {
    Range c = Range::FullCombinatorial();
    c.Normalize();
    Use(c);
  }
  auto t1 = std::chrono::high_resolution_clock::now();
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
  std::cout << "\nRange::Normalize: " << ns / 10000 << " ns/call\n";
}

TEST(Benchmark, EquityMonteCarloPerformance) {
  Range aces = Range::FromString("AA");
  Range random = Range::FullCombinatorial();
  uint8_t board[0] = {};
  auto t0 = std::chrono::high_resolution_clock::now();
  std::mt19937 rng(42);
  for (int i = 0; i < 10; i++) {
    EquityCalculator::CalculateMonteCarlo(aces, random, board, 0, 10000, rng);
  }
  auto t1 = std::chrono::high_resolution_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  std::cout << "\nEquityCalc(10K samples): " << ms / 10 << " ms/call\n";
}
