#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/evaluator/card.h"
#include "poker_engine/phase8/dashboard_gen.h"
#include "poker_engine/phase8/exploit_engine.h"
#include "poker_engine/phase8/mc_cfr_deep.h"
#include "poker_engine/phase8/multi_agent_sim.h"
#include "poker_engine/phase8/preflop_solver.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase8;
using namespace poker_engine::range;

// ============ MC-CFR DEEP ============

TEST(Phase8Test, MCCFRNodeDefaults) {
  MCNode node;
  EXPECT_EQ(node.visit_count, 0);
  EXPECT_DOUBLE_EQ(node.cumulative_utility, 0);
  EXPECT_DOUBLE_EQ(node.reach_pr_sum, 0);

  double strat[NUM_MC_ACTIONS];
  node.GetStrategy(strat);
  double sum = 0;
  for (int a = 0; a < NUM_MC_ACTIONS; a++) {
    EXPECT_GE(strat[a], 0);
    sum += strat[a];
  }
  EXPECT_NEAR(sum, 1.0, 0.001);
}

TEST(Phase8Test, MCCFRNodeRegretMatch) {
  MCNode node;
  node.cumulative_regret[static_cast<int>(MCCAction::FOLD)] = -50;
  node.cumulative_regret[static_cast<int>(MCCAction::CALL)] = 100;
  node.cumulative_regret[static_cast<int>(MCCAction::BET_POT)] = 30;
  node.cumulative_regret[static_cast<int>(MCCAction::ALL_IN)] = -20;

  double strat[NUM_MC_ACTIONS];
  node.GetRegretMatchedStrategy(strat);
  EXPECT_NEAR(strat[static_cast<int>(MCCAction::FOLD)], 0.0, 0.001);
  EXPECT_NEAR(strat[static_cast<int>(MCCAction::ALL_IN)], 0.0, 0.001);
  EXPECT_GT(strat[static_cast<int>(MCCAction::CALL)], 0.5);
  EXPECT_GT(strat[static_cast<int>(MCCAction::BET_POT)], 0.1);

  double sum = 0;
  for (int a = 0; a < NUM_MC_ACTIONS; a++) sum += strat[a];
  EXPECT_NEAR(sum, 1.0, 0.001);
}

TEST(Phase8Test, MCCFRNodeBestAction) {
  MCNode node;
  node.strategy_sum[static_cast<int>(MCCAction::CALL)] = 100;
  node.strategy_sum[static_cast<int>(MCCAction::BET_POT)] = 50;
  EXPECT_EQ(node.BestAction(), "CALL");
}

TEST(Phase8Test, MCCFRNodePruning) {
  MCNode node;
  node.cumulative_regret[0] = -100;
  node.ApplyRegretPruning(-5);
  EXPECT_GE(node.cumulative_regret[0], -5);
}

TEST(Phase8Test, MCCFRBasicSolve) {
  MCConfig config;
  config.iterations = 50;
  config.mc_samples_per_iter = 10;
  config.max_depth = 4;
  config.verbose = false;

  MCCRDeepSolver solver(config);
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+"));
  solver.SetPot(20, 7);

  auto result = solver.Solve();
  EXPECT_GE(result.total_iterations, 50);
  EXPECT_GE(result.total_time_seconds, 0);
}

TEST(Phase8Test, MCCRResultFormat) {
  MCCRResult result;
  result.total_iterations = 50;
  result.exploitability_mbb = 12.5;

  MCInfoSetKey key{0, "cc", 20, 5, 0};
  MCResultEntry entry;
  entry.avg_strategy = {0.1, 0.1, 0.5, 0.1, 0.1, 0.05, 0.0, 0.05};
  entry.best_action = "CALL";
  entry.visits = 200;
  result.strategy_map[key] = entry;

  std::string s = result.ToString();
  EXPECT_NE(s.find("MC-CFR Deep"), std::string::npos);
  EXPECT_NE(s.find("500"), std::string::npos);
  EXPECT_NE(s.find("CALL"), std::string::npos);
}

TEST(Phase8Test, MCCFRWithBoard) {
  MCConfig config;
  config.iterations = 30;
  config.mc_samples_per_iter = 10;
  config.max_depth = 4;
  config.verbose = false;

  MCCRDeepSolver solver(config);
  solver.SetHeroRange(Range::FromString("AKs,AQs"));
  solver.SetVillainRange(Range::FromString("22+,A2s+"));
  solver.SetPot(50, 25);
  solver.SetBoard({poker_engine::Card::Parse("Qh"), poker_engine::Card::Parse("Jd"),
                   poker_engine::Card::Parse("7c")});

  auto result = solver.Solve();
  EXPECT_GE(result.total_iterations, 30);
}

// ============ PREFLOP SOLVER ============

TEST(Phase8Test, PreflopMatrixInit) {
  PreflopMatrix169 matrix;
  matrix.Init();
  EXPECT_EQ(matrix.labels[0], "AA");
  EXPECT_EQ(matrix.labels[12], "22");
  int non_empty = 0;
  for (int i = 0; i < PreflopMatrix169::NUM_TYPES; i++)
    if (matrix.labels[i] != "XX") non_empty++;
  EXPECT_EQ(non_empty, 169);
}

TEST(Phase8Test, PreflopAdviceToString) {
  PreflopAdvice a;
  a.position = BTN;
  a.hand_name = "AKs";
  a.recommended_action = "3Bet";
  a.raise_freq = 0.7;
  a.call_freq = 0.3;
  a.fold_freq = 0.0;
  std::string s = a.ToString();
  EXPECT_NE(s.find("BTN"), std::string::npos);
  EXPECT_NE(s.find("AKs"), std::string::npos);
  EXPECT_NE(s.find("3Bet"), std::string::npos);
}

TEST(Phase8Test, PreflopQuickSolve) {
  // Use very few samples for fast testing
  auto advice = PreflopSolver::QuickSolve(BTN, 200);
  EXPECT_GT(advice.size(), 0);
  // Best hand should be AA
  EXPECT_EQ(advice[0].hand_name, "AA");
}

TEST(Phase8Test, PreflopHandAnalysis) {
  PreflopConfig cfg;
  cfg.mc_samples = 20;
  PreflopSolver solver(cfg);

  auto info = solver.AnalyzeHand("AKs", BTN);
  EXPECT_EQ(info.hand_name, "AKs");
  EXPECT_TRUE(info.suited);
}

// ============ EXPLOIT ENGINE ============

TEST(Phase8Test, ExploitEngineCreation) {
  ExploitEngine engine;
  engine.SetBaselineRanges(Range::FromString("22+,A2s+"), Range::FullCombinatorial());

  auto summary = engine.AnalyzeHandHistory({}, "Hero");
  EXPECT_EQ(summary.total_hands, 0);
  EXPECT_EQ(summary.total_leaked_bb, 0);
}

TEST(Phase8Test, ExploitOptimalAction) {
  ExploitEngine engine;

  std::string opt_high = engine.GetOptimalAction(0.8, 100, 25);
  EXPECT_NE(opt_high, "Fold");

  std::string opt_low = engine.GetOptimalAction(0.2, 100, 75);
  EXPECT_TRUE(opt_low == "Fold" || opt_low == "Call");
}

TEST(Phase8Test, ExploitOptimalEV) {
  ExploitEngine engine;
  double ev_high = engine.GetOptimalEV(0.9, 100, 25);
  double ev_low = engine.GetOptimalEV(0.1, 100, 25);
  EXPECT_GT(ev_high, ev_low);
}

TEST(Phase8Test, ExploitParseAction) {
  EXPECT_DOUBLE_EQ(ExploitEngine::ParseActionAmount("Call $50"), 50);
  EXPECT_DOUBLE_EQ(ExploitEngine::ParseActionAmount("Bet $123.45"), 123.45);
  EXPECT_DOUBLE_EQ(ExploitEngine::ParseActionAmount("Folds"), 0);

  EXPECT_EQ(ExploitEngine::ParseActionType("raises to $100"), "Raise");
  EXPECT_EQ(ExploitEngine::ParseActionType("All-in $50"), "All-In");
}

TEST(Phase8Test, ExploitSnapshots) {
  ExploitEngine engine;
  engine.RecordSnapshot(100, 5.0, 6.0);
  engine.RecordSnapshot(200, 4.5, 5.5);
  engine.RecordSnapshot(300, 6.0, 5.8);

  auto snaps = engine.GetSnapshots();
  EXPECT_EQ(snaps.size(), 3);

  auto trend = engine.CurrentTrend();
  EXPECT_GT(trend.hand_number, 0);
}

TEST(Phase8Test, ExploitFullReport) {
  ExploitEngine engine;
  for (int i = 1; i <= 5; i++) engine.RecordSnapshot(i * 100, 3.0 + (i % 3) * 0.5, 4.0);

  std::string report = engine.FullReport();
  EXPECT_NE(report.find("Exploit Engine"), std::string::npos);
}

// ============ DASHBOARD ============

TEST(Phase8Test, DashboardCreation) {
  DashboardGenerator gen;
  gen.SetTitle("Test Dashboard");

  std::vector<std::pair<int, double>> curve;
  for (int i = 0; i < 10; i++) curve.push_back({i * 50, 1.0 + i * 0.05});
  gen.AddEquityCurve(curve);

  auto data = gen.Generate();
  EXPECT_GT(data.series.size(), 0);

  std::string json = data.ToJSON();
  EXPECT_NE(json.find("Test Dashboard"), std::string::npos);

  std::string html = data.ToHTML();
  EXPECT_NE(html.find("<html>"), std::string::npos);
}

TEST(Phase8Test, DashboardExport) {
  DashboardGenerator gen;
  gen.SetTitle("Quick Export");

  std::vector<std::pair<int, double>> curve;
  for (int i = 0; i < 5; i++) curve.push_back({i, i * 1.5});
  gen.AddEquityCurve(curve);

  bool json_ok = gen.ExportJSON("/tmp/test_p8_dash.json");
  bool html_ok = gen.ExportHTML("/tmp/test_p8_dash.html");
  EXPECT_TRUE(json_ok);
  EXPECT_TRUE(html_ok);

  std::remove("/tmp/test_p8_dash.json");
  std::remove("/tmp/test_p8_dash.html");
}

TEST(Phase8Test, DashboardEmpty) {
  DashboardGenerator gen;
  gen.SetTitle("Empty");
  auto data = gen.Generate();
  EXPECT_EQ(data.series.size(), 0);
  EXPECT_NE(data.ToJSON().find("Empty"), std::string::npos);
}

// ============ MULTI AGENT SIMULATION ============

TEST(Phase8Test, AgentConfig) {
  AgentConfig agent{"Test", "AA,KK", 10000, AgentConfig::NIT, 3};
  EXPECT_EQ(agent.name, "Test");
  EXPECT_EQ(agent.starting_stack, 10000);
}

TEST(Phase8Test, AgentSimCreation) {
  MultiAgentSimulator sim(500);
  sim.AddAgent({"Nit", "TT+,AKs+", 10000, AgentConfig::NIT, 3});
  sim.AddAgent({"LAG", "22+", 10000, AgentConfig::LAG, 8});
  EXPECT_EQ(sim.GetAgentsCount(), 2);
}

TEST(Phase8Test, HeadToHeadEquity) {
  double eq = MultiAgentSimulator::HeadToHeadEquity("AA", "22+,A2s+", 10000);
  EXPECT_GE(eq, 0.75);
  EXPECT_LE(eq, 1.0);
}

TEST(Phase8Test, MatchupRun) {
  MultiAgentSimulator sim(200);
  sim.AddAgent("TAG_Bot", "22+,A2s+,K2s+", AgentConfig::TAG);
  sim.AddAgent("LAG_Bot", "22+,A2s+,K2s+,Q2s+", AgentConfig::LAG);

  auto result = sim.RunMatchup(0, 1, 200);
  EXPECT_EQ(result.payouts.size(), 2);
}

TEST(Phase8Test, RoundRobinSmall) {
  MultiAgentSimulator sim(100);
  sim.AddAgent({"Tight", "AA,KK,QQ,AKs", 10000, AgentConfig::NIT, 3});
  sim.AddAgent({"Medium", "22+,A2s+,K2s+", 10000, AgentConfig::TAG, 5});

  sim.SetVerbose(false);
  auto outcome = sim.RunRoundRobin();
  EXPECT_EQ(outcome.rankings.size(), 2);
  EXPECT_FALSE(outcome.matchup_matrix.empty());
}

TEST(Phase8Test, FFACreation) {
  MultiAgentSimulator sim(50);
  sim.AddAgent({"A", "AA,KK", 1000, AgentConfig::NIT});
  sim.AddAgent({"B", "22+,A2s+", 1000, AgentConfig::TAG});
  sim.AddAgent({"C", "22+,A2s+,K2s+", 1000, AgentConfig::LAG});

  auto result = sim.RunFreeForAll(25);
  EXPECT_EQ(result.payouts.size(), 3);
}

// ============ INTEGRATION ============

TEST(Phase8Test, FullPipelineMCtoExploit) {
  MCConfig mc_config;
  mc_config.iterations = 30;
  mc_config.mc_samples_per_iter = 5;
  mc_config.max_depth = 3;
  mc_config.verbose = false;

  MCCRDeepSolver solver(mc_config);
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+"));
  solver.SetPot(20, 7);

  auto mc_result = solver.Solve();
  EXPECT_GE(mc_result.total_iterations, 30);

  ExploitEngine engine;
  engine.SetBaselineRanges(Range::FromString("AKs"), Range::FromString("22+"));

  for (int i = 1; i <= 5; i++)
    engine.RecordSnapshot(i * 100, mc_result.exploitability_mbb * (0.5 + (i % 3) * 0.1),
                          mc_result.exploitability_mbb * 0.8);

  auto trend = engine.CurrentTrend();
  EXPECT_GT(trend.hand_number, 0);

  DashboardGenerator gen;
  gen.SetTitle("MC-CFR Analysis Dashboard");

  std::vector<std::pair<int, double>> curve;
  for (size_t i = 0; i < mc_result.ev_per_iteration.size(); i++)
    curve.push_back({static_cast<int>(i * 100), mc_result.ev_per_iteration[i]});
  gen.AddEquityCurve(curve);

  auto data = gen.Generate();
  EXPECT_GT(data.series.size(), 0);
}

TEST(Phase8Test, ExploitToDashboard) {
  ExploitEngine engine;
  engine.SetBaselineRanges(Range::FromString("AKs"), Range::FullCombinatorial());

  for (int i = 1; i <= 10; i++) engine.RecordSnapshot(i * 50, 2.0 + sin(i * 0.3) * 1.5, 4.0);

  DashboardGenerator gen;
  gen.SetTitle("Exploit Tracking");
  gen.AddExploitData(engine);

  auto data = gen.Generate();
  std::string html = data.ToHTML();
  EXPECT_GT(html.size(), 200);
  EXPECT_NE(html.find("Chart"), std::string::npos);
}
