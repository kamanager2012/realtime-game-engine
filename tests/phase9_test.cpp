#include <gtest/gtest.h>

#include <fstream>
#include <iostream>

#include "poker_engine/evaluator/card.h"
#include "poker_engine/phase9/advanced_batch_analyzer.h"
#include "poker_engine/phase9/pipeline_orchestrator.h"
#include "poker_engine/phase9/polarized_range.h"
#include "poker_engine/phase9/strategy_diff.h"
#include "poker_engine/phase9/variance_engine.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase9;
using namespace poker_engine::range;

// ============ VARIANCE ENGINE ============

TEST(Phase9Test, VarianceBasics) {
  VarianceEngine ve;
  EXPECT_EQ(ve.Count(), 0);

  ve.AddBB100(5.0);
  ve.AddBB100(-3.0);
  ve.AddBB100(8.0);
  ve.AddBB100(-2.0);
  ve.AddBB100(1.0);
  EXPECT_EQ(ve.Count(), 5);

  auto s = ve.ComputeSummary();
  EXPECT_EQ(s.n, 5);
  EXPECT_NEAR(s.mean, 1.8, 0.01);
  EXPECT_GT(s.std_dev, 0);
}

TEST(Phase9Test, VarianceConfidenceInterval) {
  VarianceEngine ve;
  for (int i = 0; i < 30; i++) ve.AddBB100(2.0 + (i % 5));
  auto ci = ve.ConfidenceInterval(0.95);
  EXPECT_LT(ci.first, ci.second);
}

TEST(Phase9Test, VarianceSWRR) {
  VarianceEngine ve;
  for (int i = 0; i < 100; i++) ve.AddBB100(3.0 + (i % 3));
  auto swrr = ve.ComputeSWRR();
  EXPECT_GT(swrr.bb_per_100, 0);
  EXPECT_NE(swrr.classification, "");
}

TEST(Phase9Test, VarianceHeatIndex) {
  VarianceEngine ve;
  for (int i = 0; i < 100; i++) ve.AddBB100(3.0);
  for (int i = 0; i < 10; i++) ve.AddBB100(15.0);
  auto h = ve.ComputeHeatIndex();
  EXPECT_NE(h.verdict, "");
  EXPECT_NE(h.advice, "");
}

TEST(Phase9Test, VarianceRiskOfRuin) {
  VarianceEngine ve;
  for (int i = 0; i < 50; i++) ve.AddBB100(3.0);
  double ror = ve.RiskOfRuin(1000, 100);
  EXPECT_GE(ror, 0);
  EXPECT_LE(ror, 1);
  EXPECT_LT(ror, 0.5);

  VarianceEngine ve2;
  for (int i = 0; i < 50; i++) ve2.AddBB100(-3.0);
  double ror2 = ve2.RiskOfRuin(1000, 100);
  EXPECT_GT(ror2, ror);
}

TEST(Phase9Test, VarianceRunningWindow) {
  VarianceEngine ve;
  for (int i = 0; i < 100; i++) ve.AddBB100((i % 20) - 5.0);
  auto rv = ve.RunningVariance(30);
  EXPECT_GT(rv.size(), 0);
  auto wr = ve.RunningWinRate(50);
  EXPECT_GT(wr.size(), 0);
}

TEST(Phase9Test, VarianceComparePeriods) {
  std::vector<SessionSample> pa, pb;
  for (int i = 0; i < 30; i++) {
    SessionSample s;
    s.session_id = i;
    s.hands_played = 100;
    s.bb_per_100 = 8.0 + (i % 5);
    s.std_dev = 15;
    pa.push_back(s);
  }
  for (int i = 0; i < 30; i++) {
    SessionSample s;
    s.session_id = i;
    s.hands_played = 100;
    s.bb_per_100 = 2.0 + (i % 5);
    s.std_dev = 15;
    pb.push_back(s);
  }
  auto r = ComparePeriods(pa, pb);
  EXPECT_GT(r.diff, 0);
  EXPECT_NE(r.interpretation, "");
}

// ============ STRATEGY DIFF ============

TEST(Phase9Test, StrategyDiffBasic) {
  StrategyDiffAnalyzer a;
  auto s = a.CompareRanges(Range::FromString("AKs,QQ+,AKo"), Range::FromString("22+,A2s+,K2s+"));
  EXPECT_GT(s.total_hands, 0);
  EXPECT_EQ(s.big_diffs + s.medium_diffs + s.close_matches, s.total_hands);
}

TEST(Phase9Test, StrategyDiffIdentical) {
  StrategyDiffAnalyzer a;
  Range r = Range::FromString("AA,KK,QQ");
  auto s = a.CompareRanges(r, r);
  EXPECT_EQ(s.big_diffs, 0);
  EXPECT_EQ(s.max_diff, 0);
}

TEST(Phase9Test, StrategyDiffByPosition) {
  StrategyDiffAnalyzer a;
  std::map<std::string, Range> ranges;
  ranges["UTG"] = Range::FromString("77+,A9s+");
  ranges["BTN"] = Range::FromString("22+,A2s+");
  auto s = a.CompareByPosition(ranges);
  EXPECT_GT(s.total_hands, 0);
}

TEST(Phase9Test, StrategyDiffClassify) {
  EXPECT_EQ(StrategyDiffAnalyzer::ClassifyHandDiff(0.2), "MORE_A");
  EXPECT_EQ(StrategyDiffAnalyzer::ClassifyHandDiff(-0.2), "MORE_B");
  EXPECT_EQ(StrategyDiffAnalyzer::ClassifyHandDiff(0.01), "SAME");
}

// ============ POLARIZED RANGE ============

TEST(Phase9Test, PolarizedBasic) {
  auto base = Range::FromString("22+,A2s+,K2s+,Q2s+");
  auto r = PolarizedRangeBuilder::BuildPolarized(base, {});
  EXPECT_GT(r.value_hands.NonZeroCount(), 0);
  EXPECT_GT(r.bluffs.NonZeroCount(), 0);
  EXPECT_GT(r.value_freq, 0);
}

TEST(Phase9Test, PolarizedWithBoard) {
  auto base = Range::FromString("22+,A2s+,K2s+");
  std::vector<Card> board = {Card::Parse("Qh"), Card::Parse("Jd"), Card::Parse("7c")};
  auto r = PolarizedRangeBuilder::BuildPolarized(base, board);
  EXPECT_GT(r.value_hands.NonZeroCount(), 0);
}

TEST(Phase9Test, PolarizedFromString) {
  auto r = PolarizedRangeBuilder::BuildFromString("22+,A2s+", "QhJd7c", Polarity::POLARIZED);
  EXPECT_GT(r.bluffs.NonZeroCount(), 0);
  auto r2 = PolarizedRangeBuilder::BuildFromString("22+,A2s+", "QhJd7c", Polarity::MERGED);
  EXPECT_GT(r2.merged_alternative.NonZeroCount(), 0);
}

TEST(Phase9Test, RangeOperations) {
  Range a = Range::FromString("AA,KK,QQ");
  Range b = Range::FromString("AA,KK,AKs");

  auto inter = PolarizedRangeBuilder::Intersect(a, b);
  EXPECT_GT(inter.NonZeroCount(), 0);

  auto excl = PolarizedRangeBuilder::Exclude(a, b);
  EXPECT_GE(excl.NonZeroCount(), 0);

  auto merged = PolarizedRangeBuilder::Merge(a, b);
  EXPECT_GE(merged.NonZeroCount(), a.NonZeroCount());

  double overlap = PolarizedRangeBuilder::RangeOverlap(a, b);
  EXPECT_GT(overlap, 0);
  EXPECT_LE(overlap, 1);
}

TEST(Phase9Test, RangeComparison) {
  Range a = Range::FromString("AA,KK,QQ");
  Range b = Range::FromString("AA,77");
  std::string s = PolarizedRangeBuilder::RangeComparison(a, b);
  EXPECT_NE(s.find("Overlap"), std::string::npos);
}

// ============ PIPELINE ============

TEST(Phase9Test, PipelineCreation) {
  PipelineConfig cfg;
  AnalysisPipeline p(cfg);
  EXPECT_EQ(p.HandCount(), 0);
}

TEST(Phase9Test, PipelineParseHand) {
  const std::string hh_text = R"(
PokerStars Hand #1000: Hold'em No Limit ($1/$2)
Table 'Test' 6-max Seat #1 is the button
Seat 1: Hero ($500)
Seat 2: Villain ($500)
Dealt to Hero [As Ks]
Villain: posts the small blind $1
Hero: posts the big blind $2
Hero: raises to $7
Villain: folds
*** SUMMARY ***
Total pot: $10
Hero collected $10
)";
  std::ofstream out("/tmp/test_p9_hand.txt");
  out << hh_text;
  out.close();

  PipelineConfig cfg;
  cfg.data_source = "/tmp/test_p9_hand.txt";
  cfg.hero_filter = "Hero";
  AnalysisPipeline p(cfg);
  auto r = p.Stage1_ParseHands();
  EXPECT_TRUE(r.success);
  EXPECT_GT(p.HandCount(), 0);
}

TEST(Phase9Test, AutoReportGenerator) {
  AutoReportGenerator gen;
  gen.SetTitle("Test Report");
  gen.AddSection("Section 1", "Content here");

  auto txt = gen.GenerateText();
  EXPECT_NE(txt.find("Test Report"), std::string::npos);
  EXPECT_NE(txt.find("Section 1"), std::string::npos);

  auto html = gen.GenerateHTML();
  EXPECT_NE(html.find("<html>"), std::string::npos);
}

// ============ BATCH ANALYZER ============

TEST(Phase9Test, BatchConfig) {
  BatchAnalysisConfig cfg;
  cfg.input_directory = "/tmp";
  AdvancedBatchAnalyzer a(cfg);
  EXPECT_EQ(a.GetPlayerAnalyses().size(), 0);
}

TEST(Phase9Test, BatchSummary) {
  BatchAnalysisConfig cfg;
  cfg.input_directory = "/tmp/nonexistent";
  AdvancedBatchAnalyzer a(cfg);
  a.Run();
  auto s = a.GetSummary();
  EXPECT_NE(s.find("Batch"), std::string::npos);
}

// ============ INTEGRATION ============

TEST(Phase9Test, FullPipelineVarianceToReport) {
  VarianceEngine ve;
  for (int i = 0; i < 50; i++) ve.AddBB100(2.0 + (i % 5));

  auto ss = ve.ComputeSummary();
  auto swrr = ve.ComputeSWRR();
  auto heat = ve.ComputeHeatIndex();

  AutoReportGenerator gen;
  gen.SetTitle("Variance Analysis");
  gen.AddSection("Statistics", ss.ToString());
  gen.AddSection("SWRR", swrr.ToString());
  gen.AddSection("Heat", heat.ToString());

  auto report = gen.GenerateText();
  EXPECT_GT(report.size(), 100);
  EXPECT_NE(report.find("Statistics"), std::string::npos);
}

TEST(Phase9Test, DiffToPolarizedIntegration) {
  // Compare ranges then build polarized version
  auto ra = Range::FromString("22+,A2s+,K2s+");
  auto rb = Range::FromString("77+,A9s+,K9s+");

  StrategyDiffAnalyzer da;
  auto diff = da.CompareRanges(ra, rb);
  EXPECT_GT(diff.total_hands, 0);

  auto pol = PolarizedRangeBuilder::BuildPolarized(ra, {});
  EXPECT_GT(pol.value_hands.NonZeroCount(), 0);

  double overlap = PolarizedRangeBuilder::RangeOverlap(pol.value_hands, pol.bluffs);
  EXPECT_LT(overlap, 0.5);  // Value and bluff ranges should have low overlap
}
