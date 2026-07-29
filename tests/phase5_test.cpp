#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/phase5/bulk_hh_parser.h"
#include "poker_engine/phase5/equity_matrix.h"
#include "poker_engine/phase5/hand_generator.h"
#include "poker_engine/phase5/icm_calc.h"
#include "poker_engine/phase5/regression_analyzer.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase5;
using namespace poker_engine::range;

// ============ Bulk Parser ============

TEST(Phase5Test, ParseStatsFormat) {
  ParseStats stats;
  stats.total_files = 10;
  stats.total_hands = 500;
  stats.successful = 490;
  stats.failed = 10;
  stats.hands_per_second = 5000;
  stats.sites["PokerStars"] = 300;
  stats.sites["GGNetwork"] = 200;

  std::string s = stats.ToString();
  EXPECT_NE(s.find("500"), std::string::npos);
  EXPECT_NE(s.find("PokerStars"), std::string::npos);
}

TEST(Phase5Test, HandDatabaseAddAndQuery) {
  HandDatabase db;
  HandWithMeta h1;
  h1.hh.hand_id = 1;
  h1.hh.site = "PokerStars";
  h1.hh.big_blind = 1.0;
  h1.parsed_ok = true;

  HandWithMeta h2;
  h2.hh.hand_id = 2;
  h2.hh.site = "GGNetwork";
  h2.hh.big_blind = 2.0;
  h2.parsed_ok = true;

  db.AddHand(h1);
  db.AddHand(h2);

  EXPECT_EQ(db.Count(), 2);
  EXPECT_EQ(db.GetHandsBySite("PokerStars").size(), 1u);
  EXPECT_EQ(db.GetHandsByBBRange(0.5, 1.5).size(), 1u);
}

TEST(Phase5Test, HandDatabaseAggregate) {
  HandDatabase db;
  HandWithMeta h1;
  h1.hh.total_pot = 100;
  h1.hh.seats.resize(6);
  h1.parsed_ok = true;

  db.AddHand(h1);

  auto agg = db.ComputeAggregate();
  EXPECT_EQ(agg.hand_count, 1);
  EXPECT_GT(agg.avg_pot, 0);
}

TEST(Phase5Test, HandDatabaseClear) {
  HandDatabase db;
  HandWithMeta h;
  h.parsed_ok = true;
  db.AddHand(h);
  EXPECT_EQ(db.Count(), 1);
  db.Clear();
  EXPECT_EQ(db.Count(), 0);
}

// ============ Equity Matrix ============

TEST(Phase5Test, EquityMatrixSingle) {
  Range hero = Range::FromString("AKs");
  Range villain = Range::FromString("QQ+");

  auto entry = EquityMatrixCalculator::SingleEquity(hero, villain, {}, 10000);
  EXPECT_GT(entry.equity, 0.0);
  EXPECT_LT(entry.equity, 1.0);
  EXPECT_GT(entry.trials, 0);
}

TEST(Phase5Test, EquityMatrixCalculate) {
  EquityMatrixCalculator calc;
  auto result = calc.Calculate(std::vector<std::string>{"AA", "KK", "QQ"},
                               std::vector<std::string>{"AKs", "AKo"}, 5000);

  EXPECT_EQ(result.matrix.size(), 3u);
  EXPECT_EQ(result.matrix[0].size(), 2u);
  EXPECT_GT(result.overall_avg_equity, 0.0);

  // AA should have highest equity
  EXPECT_GT(result.row_avg_equity[0], result.row_avg_equity[2]);

  std::string s = result.ToString();
  EXPECT_NE(s.find("Avg"), std::string::npos);
}

TEST(Phase5Test, EquityMatrixCSV) {
  EquityMatrixCalculator calc;
  auto result =
      calc.Calculate(std::vector<std::string>{"AA"}, std::vector<std::string>{"KK"}, 5000);

  std::string csv = result.ToCSV();
  EXPECT_NE(csv.find("AA"), std::string::npos);
  EXPECT_NE(csv.find("KK"), std::string::npos);
}

// ============ ICM Calculator ============

TEST(Phase5Test, ICMHeadsUp) {
  double payouts[] = {100.0, 60.0};
  double chips[] = {10000.0, 6000.0};
  auto result = ICMCalculator::Calculate(payouts, 2, chips, 2);

  EXPECT_GT(result.equity[0], result.equity[1]);
  EXPECT_GT(result.equity[0], 0.4);
  EXPECT_LT(result.equity[0], 100.0);
  EXPECT_GT(result.m_zone[0], 0);
}

TEST(Phase5Test, ICMThreePlayers) {
  double payouts[] = {50.0, 30.0, 20.0};
  double chips[] = {10000.0, 8000.0, 4000.0};
  auto result = ICMCalculator::Calculate(payouts, 3, chips, 3);

  EXPECT_LT(result.equity[2], result.equity[0]);
}

TEST(Phase5Test, ICMBubbleFactor) {
  double payouts[] = {500.0, 300.0, 150.0, 50.0};
  double chips[] = {20000.0, 15000.0, 5000.0, 1000.0};

  double bf = ICMCalculator::BubbleFactor(payouts, 4, chips, 4, 3);
  EXPECT_GT(bf, 0);
}

TEST(Phase5Test, ICMMValue) {
  double m = ICMCalculator::MValue(1000.0, 10.0, 0, 6);
  EXPECT_NEAR(m, 100.0 / 1.5, 0.1);
}

TEST(Phase5Test, ICMPushFoldTable) {
  auto table = ICMCalculator::PushFoldTable(10.0, 10.0);
  EXPECT_GT(table.size(), 0u);

  // AA should be a push
  bool aa_pushes = false;
  for (const auto& entry : table) {
    if (entry.hand_name == "AA" && entry.push) aa_pushes = true;
  }
  EXPECT_TRUE(aa_pushes);
}

TEST(Phase5Test, ICMResultToString) {
  double payouts[] = {100.0, 60.0};
  double chips[] = {10000.0, 6000.0};
  auto result = ICMCalculator::Calculate(payouts, 2, chips, 2);

  std::string s = result.ToString();
  EXPECT_NE(s.find("ICM"), std::string::npos);
}

TEST(Phase5Test, ICMPressureIndex) {
  double payouts[] = {500.0, 300.0, 150.0, 50.0};
  double chips[] = {20000.0, 15000.0, 5000.0, 1000.0};

  double pi = ICMCalculator::PressureIndex(payouts, 4, chips, 4, 3);
  EXPECT_GE(pi, 0.0);
  EXPECT_LE(pi, 1.0);
}

// ============ Regression Analyzer ============

TEST(Phase5Test, RegressionEmpty) {
  RegressionAnalyzer analyzer;
  auto reg = analyzer.LinearRegression();
  EXPECT_EQ(reg.n, 0);
}

TEST(Phase5Test, RegressionLinearTrend) {
  RegressionAnalyzer analyzer;
  for (int i = 0; i < 100; i++) {
    analyzer.AddPoint(i, 0.5 * i + 10 + (i % 3 - 1) * 0.1);
  }

  auto reg = analyzer.LinearRegression();
  EXPECT_NEAR(reg.slope, 0.5, 0.1);
  EXPECT_GT(reg.r_squared, 0.9);
}

TEST(Phase5Test, RegressionAddProfit) {
  RegressionAnalyzer analyzer;
  analyzer.AddProfit(1.0);
  analyzer.AddProfit(-0.5);
  analyzer.AddProfit(2.0);
  analyzer.AddProfit(-1.0);

  EXPECT_EQ(analyzer.Count(), 4);

  auto stats = analyzer.OverallStats();
  EXPECT_NEAR(stats.mean, 0.375, 0.01);
}

TEST(Phase5Test, RegressionSlidingWindow) {
  RegressionAnalyzer analyzer;
  for (int i = 0; i < 200; i++) {
    analyzer.AddProfit((i % 10 - 5) * 0.5);
  }

  auto ws = analyzer.SlidingWindow(50);
  EXPECT_EQ(ws.count, 50);
  EXPECT_GT(ws.std_dev, 0);
}

TEST(Phase5Test, RegressionHeatIndex) {
  RegressionAnalyzer analyzer;

  // Hot streak
  for (int i = 0; i < 50; i++) analyzer.AddProfit(2.0);

  double heat = analyzer.HeatIndex(20);
  EXPECT_GE(heat, 0.5);  // Should be "hot"
}

TEST(Phase5Test, RegressionStreaks) {
  RegressionAnalyzer analyzer;
  analyzer.AddProfit(1.0);
  analyzer.AddProfit(2.0);
  analyzer.AddProfit(-1.0);
  analyzer.AddProfit(-2.0);
  analyzer.AddProfit(-0.5);
  analyzer.AddProfit(3.0);

  auto streaks = analyzer.DetectStreaks();
  EXPECT_GE(streaks.size(), 2u);

  bool has_winning = false, has_losing = false;
  for (const auto& s : streaks) {
    if (s.is_winning) has_winning = true;
    if (!s.is_winning) has_losing = true;
  }
  EXPECT_TRUE(has_winning);
  EXPECT_TRUE(has_losing);
}

TEST(Phase5Test, RegressionOutliers) {
  RegressionAnalyzer analyzer;
  for (int i = 0; i < 50; i++) analyzer.AddProfit(0.5);
  analyzer.AddProfit(50.0);  // outlier
  for (int i = 0; i < 49; i++) analyzer.AddProfit(0.5);

  auto outliers = analyzer.DetectOutliers(2.0);
  EXPECT_GT(outliers.size(), 0u);
}

TEST(Phase5Test, RegressionTrendReport) {
  RegressionAnalyzer analyzer;
  for (int i = 0; i < 100; i++) analyzer.AddProfit((i % 5 - 2) * 0.5);

  std::string report = analyzer.TrendReport(50);
  EXPECT_NE(report.find("Regression"), std::string::npos);
  EXPECT_NE(report.find("Heat"), std::string::npos);
}

TEST(Phase5Test, WindowStatsConfidence95) {
  WindowStats ws;
  ws.count = 100;
  ws.mean = 5.0;
  ws.std_dev = 2.0;

  double ci = ws.Confidence95();
  EXPECT_GT(ci, 0);
  EXPECT_LT(ci, 1.0);
}

TEST(Phase5Test, RegressionResultInterpret) {
  RegressionResult reg;
  reg.slope = 0.5;
  reg.r_squared = 0.8;
  reg.n = 100;

  std::string interp = reg.Interpret();
  EXPECT_NE(interp.find("upward"), std::string::npos);
  EXPECT_NE(interp.find("strong"), std::string::npos);
}

// ============ Hand Generator ============

TEST(Phase5Test, HandGeneratorBasic) {
  HandGenerator gen(42);
  Range r = Range::FromString("AKs");

  auto [c1, c2] = gen.GenerateHand(r);
  // Should return valid cards
  EXPECT_GE(c1.Id(), 0);
  EXPECT_LT(c1.Id(), 52);
  EXPECT_GE(c2.Id(), 0);
  EXPECT_LT(c2.Id(), 52);
}

TEST(Phase5Test, HandGeneratorFullHand) {
  HandGenerator gen(42);
  HandGenConfig config;
  config.num_players = 6;

  auto hand = gen.GenerateFullHand(config);
  EXPECT_EQ(hand.player_names.size(), 6u);
  EXPECT_EQ(hand.hole_cards.size(), 6u);
  EXPECT_FALSE(hand.board.empty());
}

TEST(Phase5Test, HandGeneratorBatch) {
  HandGenerator gen(42);
  HandGenConfig config;
  config.num_players = 2;

  auto hands = gen.GenerateBatch(10, config);
  EXPECT_EQ(hands.size(), 10u);
}

TEST(Phase5Test, HandGeneratorDistribution) {
  HandGenerator gen(42);

  auto dist = gen.ComputeDistribution(1000, 6);
  EXPECT_EQ(dist.total_generated, 1000);
  EXPECT_GT(dist.hand_counts.size(), 0u);

  // AA should appear
  EXPECT_GT(dist.hand_counts.count("AA"), 0u);

  std::string s = dist.ToString();
  EXPECT_NE(s.find("1000"), std::string::npos);
}

TEST(Phase5Test, HandGeneratorPositionRange) {
  auto sb_range = HandGenerator::GetPositionRange(0);
  auto btn_range = HandGenerator::GetPositionRange(5);

  EXPECT_GT(btn_range.NonZeroCount(), sb_range.NonZeroCount());

  EXPECT_STREQ(HandGenerator::PositionName(0), "SB");
  EXPECT_STREQ(HandGenerator::PositionName(5), "BTN");
}
