#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/phase2/range_builder.h"
#include "poker_engine/phase2/range_visualizer.h"
#include "poker_engine/phase2/solver_node.h"
#include "poker_engine/range/range.h"

using namespace poker_engine::phase2;
using poker_engine::Card;
using poker_engine::range::Range;

// =========== Range Builder 测试 ===========

TEST(Phase2Test, PositionNames) {
  EXPECT_STREQ(PositionName[0], "SB");
  EXPECT_STREQ(PositionName[7], "BTN");
}

TEST(Phase2Test, Get6MaxSpec) {
  const auto& spec = RangeBuilder::Get6MaxSpec(Position::BTN);
  EXPECT_GT(spec.raise_pct, 0.5);
  EXPECT_LT(spec.raise_pct, 1.0);

  const auto& utg_spec = RangeBuilder::Get6MaxSpec(Position::UTG);
  EXPECT_LT(utg_spec.raise_pct, spec.raise_pct);
}

TEST(Phase2Test, BuildKnownPositionRange) {
  RangeBuilder builder;
  Range btn = builder.Build("BTN");
  Range utg = builder.Build("UTG");

  EXPECT_GT(btn.NonZeroCount(), utg.NonZeroCount());

  Range full = Range::FullCombinatorial();
  EXPECT_LT(utg.NonZeroCount(), full.NonZeroCount());
  EXPECT_GT(btn.NonZeroCount(), full.NonZeroCount() * 0.9);
}

TEST(Phase2Test, BuildFromString) {
  RangeBuilder builder;
  Range r = builder.Build("AA,KK,QQ");
  EXPECT_EQ(r.NonZeroCount(), 18);
}

// =========== Range Visualizer 测试 ===========

TEST(Phase2Test, VisualizerEmptyRange) {
  Range r;
  RangeVisualizer viz(r);
  std::string ascii = viz.ToFullASCII();
  EXPECT_NE(ascii.find("Pairs"), std::string::npos);
  EXPECT_NE(ascii.find("Suited"), std::string::npos);
  EXPECT_NE(ascii.find("Offsuit"), std::string::npos);
  EXPECT_EQ(r.NonZeroCount(), 0);
}

TEST(Phase2Test, VisualizerFullRange) {
  Range r = Range::FullCombinatorial();
  RangeVisualizer viz(r);
  std::string ascii = viz.ToFullASCII();
  EXPECT_NE(ascii.find("1326"), std::string::npos);
}

TEST(Phase2Test, VisualizerHTML) {
  Range r = Range::FromString("AA,KK,AKs");
  RangeVisualizer viz(r);

  std::string html = viz.ToHTML();
  EXPECT_NE(html.find("<html>"), std::string::npos);
  EXPECT_NE(html.find("<table>"), std::string::npos);
  EXPECT_NE(html.find("Combos:"), std::string::npos);
}

TEST(Phase2Test, VisualizerOutputFormat) {
  Range r = Range::FromString("88+,A2s+,K2s+,Q2s+");
  RangeVisualizer viz(r);

  std::string pairs = viz.ToASCII(RangeVisualizer::HandType::Pairs);
  EXPECT_NE(pairs.find("Pairs"), std::string::npos);

  std::string suited = viz.ToASCII(RangeVisualizer::HandType::Suited);
  EXPECT_NE(suited.find("Suited"), std::string::npos);
}

// =========== CFRA Solver 测试 ===========

TEST(Phase2Test, CFranodeDefault) {
  CFranode node;
  EXPECT_EQ(node.visit_count, 0);

  double strategy[NUM_ACTIONS];
  node.GetStrategy(strategy);
  for (int a = 0; a < NUM_ACTIONS; a++) {
    EXPECT_NEAR(strategy[a], 1.0 / NUM_ACTIONS, 0.01);
  }
}

TEST(Phase2Test, CFranodeAfterRegret) {
  CFranode node;
  node.cumulative_regret[static_cast<int>(Action::FOLD)] = -10;
  node.cumulative_regret[static_cast<int>(Action::CALL)] = 50;
  node.cumulative_regret[static_cast<int>(Action::POT)] = 30;

  double strategy[NUM_ACTIONS];
  node.GetStrategy(strategy);

  EXPECT_NEAR(strategy[static_cast<int>(Action::FOLD)], 0.0, 0.001);
  EXPECT_GT(strategy[static_cast<int>(Action::CALL)], 0.3);
  EXPECT_GT(strategy[static_cast<int>(Action::POT)], 0.2);

  double sum = 0;
  for (int a = 0; a < NUM_ACTIONS; a++) sum += strategy[a];
  EXPECT_NEAR(sum, 1.0, 0.001);
}

TEST(Phase2Test, CFranodeAverageStrategy) {
  CFranode node;
  node.strategy_sum[0] = 10;
  node.strategy_sum[1] = 5;
  node.strategy_sum[2] = 3;

  double avg[NUM_ACTIONS];
  node.GetAverageStrategy(avg);

  double sum = 0;
  for (int a = 0; a < NUM_ACTIONS; a++) sum += avg[a];
  EXPECT_NEAR(sum, 1.0, 0.001);
}

TEST(Phase2Test, SolverBasic) {
  SolverConfig config;
  config.iterations = 100;
  config.n_samples = 1000;
  config.verbose = false;

  CFRSolver solver(config);

  RangeVector ranges;
  ranges.push_back(Range::FromString("AKs"));
  ranges.push_back(Range::FromString("22+,A2s+,K2s+,Q2s+"));

  solver.SetRanges(ranges);
  solver.SetBoard({Card::Parse("Qd"), Card::Parse("Jh"), Card::Parse("7s")}, 1);

  auto result = solver.Solve();
  EXPECT_GT(result.strategy_profile.size(), 0);
  std::cout << result.ToString() << "\n";
}

TEST(Phase2Test, SolverMultipleIterations) {
  SolverConfig config;
  config.iterations = 500;
  config.n_samples = 2000;
  config.verbose = false;

  CFRSolver solver(config);

  RangeVector ranges;
  ranges.push_back(Range::FromString("AA"));
  ranges.push_back(Range::FromString("22+,A2s+,K2s+"));

  solver.SetRanges(ranges);
  solver.SetBoard({Card::Parse("Kc"), Card::Parse("7h"), Card::Parse("2d")}, 1);

  auto result = solver.Solve();
  EXPECT_GT(result.strategy_profile.size(), 0);
  EXPECT_GE(result.exploitability, 0);
}

TEST(Phase2Test, ActionNames) {
  EXPECT_STREQ(ActionName[static_cast<int>(Action::FOLD)], "FOLD");
  EXPECT_STREQ(ActionName[static_cast<int>(Action::CHECK)], "CHECK");
  EXPECT_STREQ(ActionName[static_cast<int>(Action::CALL)], "CALL");
  EXPECT_STREQ(ActionName[static_cast<int>(Action::BET_33)], "BET_33");
  EXPECT_STREQ(ActionName[static_cast<int>(Action::ALL_IN)], "ALL_IN");
}

// =========== ICM 测试 ===========

TEST(Phase2Test, ICMHeadsUp) {
  double payouts[] = {100.0, 60.0};
  double chips[] = {10000.0, 6000.0};
  double bb = 10;
  auto result = RangeBuilder::CalculateICM(payouts, 2, chips, 2, bb);

  EXPECT_GT(result.equity[0], result.equity[1]);
  EXPECT_GT(result.equity[0], 0.4);
  EXPECT_LT(result.equity[0], 1.0);
  EXPECT_DOUBLE_EQ(result.m_zone_stack[0], 1000);
  EXPECT_DOUBLE_EQ(result.m_zone_stack[1], 600);
}

TEST(Phase2Test, ICMThreePlayers) {
  double payouts[] = {50.0, 30.0, 20.0};
  double chips[] = {10000.0, 8000.0, 4000.0};
  double bb = 10;
  auto result = RangeBuilder::CalculateICM(payouts, 3, chips, 3, bb);

  double total_equity = 0;
  for (int i = 0; i < 3; i++) total_equity += result.equity[i];
  EXPECT_NEAR(total_equity, 1.0, 0.01);
  EXPECT_LT(result.equity[2], result.equity[0]);
}

TEST(Phase2Test, ICMShortStackBubble) {
  double payouts[] = {500.0, 300.0, 150.0, 50.0};
  double chips[] = {20000.0, 15000.0, 5000.0, 1000.0};
  double bb = 50;

  auto result = RangeBuilder::CalculateICM(payouts, 4, chips, 4, bb);
  EXPECT_LT(result.m_zone_stack[3], 50);
  EXPECT_GT(result.equity[0], result.equity[3]);
}

// =========== Integration ===========

TEST(Phase2Test, FullPipelineUTG) {
  RangeBuilder builder;
  Range utg_range = builder.Build("UTG");

  RangeVisualizer viz(utg_range);
  std::string report = viz.ToFullASCII();

  EXPECT_GT(utg_range.NonZeroCount(), 0);
  EXPECT_LT(utg_range.NonZeroCount(), Range::FullCombinatorial().NonZeroCount());
  EXPECT_NE(report.find("Pairs"), std::string::npos);
  EXPECT_NE(report.find("Suited"), std::string::npos);
  EXPECT_NE(report.find("Offsuit"), std::string::npos);
}

TEST(Phase2Test, AllPositionsHaveValidRanges) {
  RangeBuilder builder;
  for (int i = 0; i < static_cast<int>(Position::_COUNT); i++) {
    std::string name = PositionName[i];
    Range r = builder.Build(name);
    EXPECT_GT(r.NonZeroCount(), 0) << "Position " << name << " has 0 combos";
    EXPECT_LE(r.NonZeroCount(), 1326) << "Position " << name << " has >1326 combos";
  }
}

TEST(Phase2Test, SolveResultToString) {
  SolveResult result;
  result.iterations = 100;
  result.exploitability = 5.2;
  result.total_ev = 1.5;

  std::string s = result.ToString();
  EXPECT_NE(s.find("CFRA"), std::string::npos);
  EXPECT_NE(s.find("100"), std::string::npos);
  EXPECT_NE(s.find("5.2"), std::string::npos);
}
