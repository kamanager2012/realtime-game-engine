#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/phase3/batch_simulator.h"
#include "poker_engine/phase3/flop_explorer.h"
#include "poker_engine/phase3/hand_replayer.h"
#include "poker_engine/phase3/spot_solver.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase3;
using namespace poker_engine::range;
using poker_engine::phase1::ActionType;
using poker_engine::phase1::HandHistory;
using poker_engine::phase1::PlayerAction;
using poker_engine::phase1::StreetRound;
namespace P1 = poker_engine::phase1;

// ========= FLOP EXPLORER ===========

TEST(Phase3Test, FlopExplorerBasic) {
  FlopExplorer explorer;
  explorer.SetHeroRange(Range::FromString("AKs,QQ+,AKo"));
  explorer.SetVillainRange(Range::FromString("22+,A2s+,K2s+,Q2s+"));

  auto result = explorer.AnalyzeFlop("QdJh7s", 10000);
  EXPECT_GT(result.hero_equity, 0.0);
  EXPECT_GT(result.villain_equity, 0.0);
  EXPECT_NEAR(result.hero_equity + result.villain_equity, 1.0, 0.02);

  std::string s = result.ToString();
  EXPECT_NE(s.find("QdJh7s"), std::string::npos);
}

TEST(Phase3Test, FlopDryRag) {
  FlopExplorer explorer;
  explorer.SetHeroRange(Range::FromString("AA,KK,QQ"));
  explorer.SetVillainRange(Range::FromString("22+,A2s+"));

  auto result = explorer.AnalyzeFlop("2h7d9c", 10000);
  EXPECT_GT(result.hero_equity, 0.6);
}

TEST(Phase3Test, FlopMonotone) {
  FlopExplorer explorer;
  explorer.SetHeroRange(Range::FromString("AKs, AQs"));
  explorer.SetVillainRange(Range::FromString("22+,A2s+,K2s+"));

  auto result = explorer.AnalyzeFlop("AsKsQs", 10000);
  EXPECT_GT(result.hero_equity, 0.0);
  EXPECT_LT(result.hero_equity, 1.0);
}

TEST(Phase3Test, FlopCategoryPaired) {
  EXPECT_TRUE(FlopExplorer::IsPairedFlop("KcKd7s"));
  EXPECT_TRUE(FlopExplorer::IsPairedFlop("7cAs7d"));
  EXPECT_FALSE(FlopExplorer::IsPairedFlop("KcQd7s"));
}

TEST(Phase3Test, FlopCategoryMonotone) {
  EXPECT_TRUE(FlopExplorer::IsMonotoneFlop("AsKsQs"));
  EXPECT_FALSE(FlopExplorer::IsMonotoneFlop("AsKdQs"));
}

TEST(Phase3Test, FlopCategoryConnected) {
  EXPECT_TRUE(FlopExplorer::IsConnectedFlop("KdQhJs"));
  EXPECT_TRUE(FlopExplorer::IsConnectedFlop("9s8h7d"));
  EXPECT_FALSE(FlopExplorer::IsConnectedFlop("As7d2h"));
}

TEST(Phase3Test, TurnRunoutAnalysis) {
  FlopExplorer explorer;
  explorer.SetHeroRange(Range::FromString("AKs"));
  explorer.SetVillainRange(Range::FromString("22+,A2s+"));

  auto stats = explorer.AnalyzeTurnRunouts("QdJh7s", 5000);
  EXPECT_EQ(stats.total_runouts, 49);  // 52 - 3 (flop cards) = 49 remaining
  EXPECT_GT(stats.avg_equity, 0.0);
}

// ========= SPOT SOLVER ===========

TEST(Phase3Test, SpotSolverNutsOnBoard) {
  auto result = SpotSolver::QuickSolve("AKs", "22+,A2s+,K2s+", "QdJhTs", 100, 30);

  EXPECT_GT(result.hero_ev, 0);
  EXPECT_NE(result.best_action, nullptr);
  std::cout << result.ToString() << "\n";
}

TEST(Phase3Test, SpotSolverOverpair) {
  auto result = SpotSolver::QuickSolve("AA", "22+,A2s+,K2s+", "Kd7h2c", 100, 30);

  EXPECT_GT(result.hero_ev, 0);
  std::cout << result.ToString() << "\n";
}

TEST(Phase3Test, SpotSolverDrawyBoard) {
  auto result = SpotSolver::QuickSolve("AKs", "TT+,AQs+", "AsKs2h", 100, 50);

  EXPECT_NE(result.best_action, nullptr);
}

TEST(Phase3Test, SpotSolverActionGeneration) {
  SpotSolver solver;
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+"));
  solver.SetBoard({"Qd", "Jh", "7s"});
  solver.SetPot(100);
  solver.SetToCall(30);
  solver.SetSamples(10000);

  auto result = solver.Solve();
  EXPECT_GT(result.actions.size(), 0u);
  EXPECT_GT(result.actions.size(), 1u);
}

TEST(Phase3Test, SpotSolvePureBestIsReasonable) {
  auto result = SpotSolver::QuickSolve("AA", "KK", "AsKsQs", 100, 30);

  EXPECT_NE(result.best_action->desc, "Fold");
  EXPECT_GT(result.best_action->ev, 0);
  std::cout << result.ToString() << "\n";
}

// ========= HAND REPLAYER ===========

TEST(Phase3Test, HandReplayerEmptyHistory) {
  HandHistory hh;
  HandReplayer replayer;
  auto result = replayer.Replay(hh);
  EXPECT_EQ(result.nodes.size(), 0u);
}

TEST(Phase3Test, HandReplayerSimpleHand) {
  HandHistory hh;
  hh.hand_id = 1;
  hh.hero_name = "Hero";
  hh.game_type = "No Limit Hold'em";
  hh.starting_stacks["Hero"] = 100;
  hh.starting_stacks["Villain"] = 100;

  hh.hero_cards[0] = Card::Parse("As");
  hh.hero_cards[1] = Card::Parse("Ks");
  hh.hero_cards_known_ = true;

  StreetRound flop;
  flop.street = P1::Street::FLOP;
  flop.pot_before = 20;
  flop.community_cards = {Card(Rank::Queen, Suit::Diamonds), Card(Rank::Jack, Suit::Hearts),
                          Card(Rank::Seven, Suit::Clubs)};

  PlayerAction bet;
  bet.player_name = "Hero";
  bet.action = ActionType::BET;
  bet.amount = 15;
  bet.street = P1::Street::FLOP;
  bet.total_invested = 15;
  flop.actions.push_back(bet);

  hh.streets.push_back(flop);
  hh.board = flop.community_cards;
  hh.total_pot = 35;

  HandReplayer replayer;
  replayer.SetVillainRange("22+,A2s+,K2s+,Q2s+");

  auto result = replayer.Replay(hh);
  EXPECT_GT(result.nodes.size(), 0u);
  EXPECT_EQ(result.hero_name, "Hero");

  std::string summary = result.Summary();
  EXPECT_NE(summary.find("Hero"), std::string::npos);
}

TEST(Phase3Test, HandReplayerFullHand) {
  HandHistory hh;
  hh.hand_id = 2;
  hh.hero_name = "Hero";
  hh.game_type = "No Limit Hold'em";
  hh.starting_stacks["Hero"] = 100;
  hh.starting_stacks["Villain"] = 100;

  hh.hero_cards[0] = Card::Parse("As");
  hh.hero_cards[1] = Card::Parse("Ks");
  hh.hero_cards_known_ = true;

  StreetRound preflop;
  preflop.street = P1::Street::PREFLOP;
  preflop.pot_before = 3;
  PlayerAction raise1;
  raise1.player_name = "Villain";
  raise1.action = ActionType::RAISE;
  raise1.amount = 10;
  raise1.street = P1::Street::PREFLOP;
  raise1.total_invested = 10;
  PlayerAction call1;
  call1.player_name = "Hero";
  call1.action = ActionType::CALL;
  call1.amount = 10;
  call1.street = P1::Street::PREFLOP;
  call1.total_invested = 10;
  preflop.actions = {raise1, call1};
  hh.streets.push_back(preflop);

  StreetRound flop;
  flop.street = P1::Street::FLOP;
  flop.pot_before = 23;
  flop.community_cards = {Card(Rank::Queen, Suit::Diamonds), Card(Rank::Jack, Suit::Hearts),
                          Card(Rank::Deuce, Suit::Spades)};
  PlayerAction bet;
  bet.player_name = "Villain";
  bet.action = ActionType::BET;
  bet.amount = 15;
  bet.street = P1::Street::FLOP;
  bet.total_invested = 15;
  PlayerAction call2;
  call2.player_name = "Hero";
  call2.action = ActionType::CALL;
  call2.amount = 15;
  call2.street = P1::Street::FLOP;
  call2.total_invested = 15;
  flop.actions = {bet, call2};
  hh.streets.push_back(flop);

  hh.board = flop.community_cards;
  hh.total_pot = 53;

  HandReplayer replayer;
  replayer.SetVillainRange("22+,A2s+,K2s+,Q2s+");

  auto result = replayer.Replay(hh);
  EXPECT_GT(result.nodes.size(), 0u);

  std::cout << result.FullReport() << "\n";
}

TEST(Phase3Test, ReplayNodeToString) {
  ReplayNode node;
  node.street_name = "Flop";
  node.player = "Hero";
  node.action_desc = "bets $15";
  node.pot_before = 20;
  node.amount = 15;
  node.hero_equity = 0.55;
  node.recommendation = "+";

  std::string s = node.ToString();
  EXPECT_NE(s.find("Flop"), std::string::npos);
  EXPECT_NE(s.find("Hero"), std::string::npos);
  EXPECT_NE(s.find("bets"), std::string::npos);
}

// ========= BATCH SIMULATOR ===========

TEST(Phase3Test, BatchSimulatorBasic) {
  BatchConfig config;
  config.iterations = 2000;
  config.initial_stack = 100;
  config.big_blind = 1;
  config.verbose = false;

  BatchSimulator sim(config);
  sim.AddPlayer("Hero", "AA");
  sim.AddPlayer("Villain", "22+,A2s+");

  auto results = sim.Run(50);
  EXPECT_EQ(results.size(), 50u);

  auto stats = sim.GetStats();
  EXPECT_EQ(stats.size(), 2u);
  EXPECT_GT(stats.at("Hero").hands_played, 0);
}

TEST(Phase3Test, BatchSimulatorStatsFormat) {
  BatchConfig config;
  config.iterations = 1000;
  config.initial_stack = 100;
  config.big_blind = 1;

  BatchSimulator sim(config);
  sim.AddPlayer("P1", "AA");
  sim.AddPlayer("P2", "22+");
  sim.Run(20);

  std::string report = sim.StatsReport();
  EXPECT_NE(report.find("P1"), std::string::npos);
  EXPECT_NE(report.find("P2"), std::string::npos);
  EXPECT_NE(report.find("Batch"), std::string::npos);
}

TEST(Phase3Test, BatchSimulatorMultiway) {
  BatchConfig config;
  config.iterations = 2000;
  config.initial_stack = 100;
  config.big_blind = 1;
  config.verbose = false;

  BatchSimulator sim(config);
  sim.AddPlayer("UTG", "77+,A2s+,K9s+");
  sim.AddPlayer("CO", "55+,A2s+,K8s+");
  sim.AddPlayer("BTN", "33+,A2o+");

  auto results = sim.Run(30);
  EXPECT_EQ(results.size(), 30u);
}

// ========= INTEGRATION ===========

TEST(Phase3Test, EdgeCaseNoBoard) {
  auto result = SpotSolver::QuickSolve("AKs", "QQ+", "", 100, 30);
  EXPECT_NE(result.best_action, nullptr);
}

TEST(Phase3Test, EdgeCaseAllInPreflop) {
  auto result = SpotSolver::QuickSolve("AA", "KK+", "", 20, 20);
  EXPECT_NE(result.best_action, nullptr);
}

TEST(Phase3Test, ReplayerVsSolverConsistency) {
  FlopExplorer explorer;
  explorer.SetHeroRange(Range::FromString("AKs"));
  explorer.SetVillainRange(Range::FromString("22+,A2s+"));
  auto flop_result = explorer.AnalyzeFlop("QhJd7c", 10000);

  SpotSolver solver;
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+,A2s+"));
  solver.SetBoard({"Qh", "Jd", "7c"});
  double equity = solver.ComputeEquity();

  EXPECT_NEAR(flop_result.hero_equity, equity, 0.03);
}
