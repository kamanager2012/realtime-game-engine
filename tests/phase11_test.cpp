#include <gtest/gtest.h>

#include <chrono>
#include <iostream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/phase10/parallel_preflop_lut.h"
#include "poker_engine/phase11/decision_engine.h"
#include "poker_engine/phase11/engine_registry.h"
#include "poker_engine/phase11/fast_preflop_solver.h"
#include "poker_engine/phase11/solver_manager.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase11;
using namespace poker_engine::range;
using namespace poker_engine::equity;

// ============ FAST PREFLOP SOLVER ============

TEST(Phase11Test, FastPreflopCreation) {
  FastPreflopSolver solver;
  solver.BuildLUT("/tmp/test_fast.lut");
  EXPECT_TRUE(solver.LoadLUT("/tmp/test_fast.lut"));
}

TEST(Phase11Test, FastPreflopSolveBTN) {
  FastPreflopSolver solver;
  solver.BuildLUT("/tmp/test_fast2.lut");

  PositionVars opp{0.25, 0.15, 0.05};
  auto result = solver.SolvePosition("BTN", opp);

  EXPECT_GT(result.advice.size(), 0u);
  EXPECT_GT(result.solve_time_ms, 0.0);
  EXPECT_EQ(result.hands_evaluated, 169);

  // Top hand should be AA or KK (LUT sampling can vary at low sample counts)
  EXPECT_TRUE(result.advice[0].hand_name == "AA" || result.advice[0].hand_name == "KK");
  EXPECT_GT(result.advice[0].score, 70);
}

TEST(Phase11Test, FastPreflopAllPositions) {
  FastPreflopSolver solver;
  solver.BuildLUT("/tmp/test_fast3.lut");

  auto all = solver.SolveAllPositions();
  EXPECT_GT(all.size(), 0u);

  int btn_count = 0;
  for (const auto& [name, _] : all) {
    if (name.find("BTN") != std::string::npos) btn_count++;
  }
  EXPECT_GE(btn_count, 3);
}

TEST(Phase11Test, FastPreflopCompareStrategies) {
  FastPreflopSolver solver;
  solver.BuildLUT("/tmp/test_fast4.lut");

  auto result = solver.CompareStrategies("AKs,QQ", "22+,A2s+,K2s+");
  EXPECT_GT(result.advice.size(), 0u);
}

TEST(Phase11Test, FastPreflopSingleHand) {
  FastPreflopSolver solver;
  solver.BuildLUT("/tmp/test_fast5.lut");

  PositionVars opp{0.25, 0.15, 0.05};
  auto advice = solver.AnalyzeSingleHand("AKs", "BTN", opp);

  EXPECT_EQ(advice.hand_name, "AKs");
  EXPECT_GT(advice.score, 50);
}

TEST(Phase11Test, FastPreflopResultTopN) {
  FastPreflopSolver solver;
  solver.BuildLUT("/tmp/test_fast6.lut");

  PositionVars opp{0.25, 0.15, 0.05};
  auto result = solver.SolvePosition("CO", opp);

  auto top5 = result.TopN(5);
  EXPECT_LE(top5.size(), 5u);
  EXPECT_GT(top5.size(), 0u);
  EXPECT_TRUE(top5[0].hand_name == "AA" || top5[0].hand_name == "KK");
}

// ============ DECISION ENGINE ============

TEST(Phase11Test, DecisionEngineCreation) {
  DecisionEngine engine(DecisionLevel::QUICK);
  EXPECT_NO_THROW(engine.Reset());
}

TEST(Phase11Test, DecisionEnginePreFlop) {
  DecisionEngine engine(DecisionLevel::QUICK);

  GameContext ctx;
  ctx.hero_cards = "AKs";
  ctx.street = 0;
  ctx.position = 7;
  ctx.pot = 20;
  ctx.to_call = 0;
  ctx.villain_tendency = "medium";

  auto result = engine.Decide(ctx);
  EXPECT_NE(result.recommended_action, "");
  EXPECT_GE(result.confidence, 0.0);
  EXPECT_LE(result.confidence, 1.0);
}

TEST(Phase11Test, DecisionEnginePostFlop) {
  DecisionEngine engine(DecisionLevel::STANDARD);
  engine.SetVillainRange("22+,A2s+,K2s+");

  GameContext ctx;
  ctx.hero_cards = "AKs";
  ctx.street = 1;
  ctx.community_cards = {"Qh", "Jd", "7c"};
  ctx.pot = 100;
  ctx.to_call = 30;
  ctx.villain_tendency = "tight";

  auto result = engine.Decide(ctx);

  EXPECT_FALSE(result.action_ev.empty());
  EXPECT_GT(result.compute_time_ms, 0.0);
}

TEST(Phase11Test, DecisionEngineSetLevel) {
  DecisionEngine engine(DecisionLevel::QUICK);
  engine.SetLevel(DecisionLevel::STANDARD);
  engine.Reset();
  EXPECT_NO_THROW(engine.Reset());
}

// ============ SOLVER MANAGER ============

TEST(Phase11Test, SolverManagerInit) {
  auto& manager = SolverManager::Instance();
  manager.Initialize();
  EXPECT_NO_THROW(manager.GetDecisionEngine().Reset());
}

TEST(Phase11Test, SolverManagerEquityQuery) {
  auto& manager = SolverManager::Instance();
  manager.Initialize();

  auto result = manager.Analyze("AKs vs QQ");
  // Output should contain equity percentage
  EXPECT_NE(result.content.find("%"), std::string::npos);
}

TEST(Phase11Test, SolverManagerPreflopQuery) {
  auto& manager = SolverManager::Instance();
  manager.Initialize();

  auto result = manager.Analyze("preflop BTN");
  EXPECT_NE(result.content.find("BTN"), std::string::npos);
}

TEST(Phase11Test, SolverManagerHandQuery) {
  auto& manager = SolverManager::Instance();
  manager.Initialize();

  auto result = manager.Analyze("AKs BTN");
  EXPECT_NE(result.content.find("AKs"), std::string::npos);
}

// ============ ENGINE REGISTRY ============

TEST(Phase11Test, RegistryBasics) {
  EngineRegistry::Instance().Register(
      "test_engine", "A test engine",
      [](const std::string& input) -> std::string { return "Echo: " + input; });

  EXPECT_TRUE(EngineRegistry::Instance().HasEngine("test_engine"));

  auto result = EngineRegistry::Instance().Execute("test_engine", "hello");
  EXPECT_EQ(result, "Echo: hello");
}

TEST(Phase11Test, RegistryList) {
  auto list = EngineRegistry::Instance().ListEngines();
  EXPECT_GT(list.size(), 0u);
}

TEST(Phase11Test, RegistryUnregister) {
  EngineRegistry::Instance().Unregister("test_engine");
  EXPECT_FALSE(EngineRegistry::Instance().HasEngine("test_engine"));
}

TEST(Phase11Test, RegistryMissingEngine) {
  auto result = EngineRegistry::Instance().Execute("nonexistent", "test");
  EXPECT_NE(result.find("not found"), std::string::npos);
}

// ============ INTEGRATION ============

TEST(Phase11Test, FullDecisionPipeline) {
  auto& manager = SolverManager::Instance();
  manager.Initialize();

  // Pre-flop equity check
  auto r1 = manager.Analyze("AKs vs 72o");
  EXPECT_NE(r1.content.find("%"), std::string::npos);

  // Preflop strategy
  auto r2 = manager.Analyze("preflop HJ");
  EXPECT_NE(r2.content.find("HJ"), std::string::npos);
}

TEST(Phase11Test, LUTConsistencyWithMC) {
  // Compare LUT-based equity vs full MC equity
  PreflopLUTCalculator calc;
  auto lut = calc.Calculate(2000);

  float lut_eq = PreflopLUTCalculator::QueryEquity(lut, "AKs", "KQs");

  // Full MC calculation
  Range hero = Range::FromString("AKs");
  Range villain = Range::FromString("KQs");
  uint8_t board5[5] = {0};

  std::mt19937 rng(42);
  auto mc_result = EquityCalculator::CalculateMonteCarlo(hero, villain, board5, 0, 2000, rng);

  // Should be close (within margin)
  EXPECT_NEAR(lut_eq, mc_result.equity[0], 0.10);
}

TEST(Phase11Test, QuickDecisionPerformance) {
  DecisionEngine engine(DecisionLevel::QUICK);

  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 50; i++) {
    GameContext ctx;
    ctx.hero_cards = "AKs";
    ctx.street = 0;
    ctx.to_call = 0;
    ctx.pot = 20;
    auto r = engine.Decide(ctx);
  }
  auto end = std::chrono::high_resolution_clock::now();
  double elapsed = std::chrono::duration<double, std::milli>(end - start).count();

  std::cout << "50 quick decisions: " << elapsed << "ms (" << elapsed / 50 << "ms per decision)\n";
}

TEST(Phase11Test, SolverManagerUnrecognizedQuery) {
  auto& manager = SolverManager::Instance();
  manager.Initialize();

  auto result = manager.Analyze("xyz");
  // Should still return something, not crash
  EXPECT_FALSE(result.content.empty());
}

TEST(Phase11Test, SolverManagerEmptyQuery) {
  auto& manager = SolverManager::Instance();
  manager.Initialize();

  auto result = manager.Analyze("");
  EXPECT_FALSE(result.content.empty());
}
