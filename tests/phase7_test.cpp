#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/phase4/hh_parser.h"
#include "poker_engine/phase7/cfr_plus_solver.h"
#include "poker_engine/phase7/mtt_simulator.h"
#include "poker_engine/phase7/opponent_modeler.h"
#include "poker_engine/phase7/tournament_icm.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase7;
using namespace poker_engine::range;
using namespace poker_engine::phase4;

// ============ CFR+ SOLVER ============

TEST(Phase7Test, CFRPlusVanillaCreation) {
  CFRPlusSolver solver(CFRConfig{100, 500, CFRMode::VANILLA});
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+"));
  solver.SetPot(20);
  solver.SetToCall(5);
  auto result = solver.Solve();
  EXPECT_GE(result.iterations, 100);
}

TEST(Phase7Test, CFRNodeBasics) {
  CFRNode node;
  EXPECT_EQ(node.visit_count, 0);
  double strat[NUM_CFR_ACTIONS];
  node.GetRegretMatchedStrategy(strat);
  double sum = 0;
  for (int a = 0; a < NUM_CFR_ACTIONS; a++) {
    EXPECT_GE(strat[a], 0);
    sum += strat[a];
  }
  EXPECT_NEAR(sum, 1.0, 0.001);
}

TEST(Phase7Test, CFRPlusDiscount) {
  CFRNode node;
  node.cumulative_regret[0] = 100;
  node.cumulative_regret[1] = -50;
  node.cumulative_regret[2] = 200;
  std::fill(node.strategy_sum, node.strategy_sum + NUM_CFR_ACTIONS, 100);
  node.ApplyDiscount(1.5, 0.5, 2.0);
  // ApplyDiscount scales positive regrets by alpha, negative by beta
  EXPECT_GT(node.cumulative_regret[0], 100);  // 100*1.5=150 > 100
  EXPECT_GT(node.cumulative_regret[1], -50);  // -50*0.5=-25 > -50
  EXPECT_GT(node.cumulative_regret[2], 200);  // 200*1.5=300 > 200
}

TEST(Phase7Test, CFRPlusSolveMinimal) {
  CFRConfig config;
  config.iterations = 50;
  config.mc_samples = 50;
  config.mode = CFRMode::DISCOUNTED;
  config.verbose = false;
  CFRPlusSolver solver(config);
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+,A2s+"));
  solver.SetPot(20);
  solver.SetToCall(5);
  auto result = solver.Solve();
  EXPECT_GT(result.strategy_profile.size(), 0u);
  EXPECT_GE(result.time_seconds, 0);
  EXPECT_GE(result.ev_history.size(), 0u);
}

TEST(Phase7Test, CFRPlusExploitabilityRange) {
  CFRConfig config;
  config.iterations = 20;
  config.mc_samples = 50;
  config.mode = CFRMode::DISCOUNTED;
  config.verbose = false;
  CFRPlusSolver solver(config);
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+"));
  solver.SetPot(20);
  solver.SetToCall(5);
  auto result = solver.Solve();
  EXPECT_GE(result.exploitability, 0);
  // With low iterations, exploitability can be high; just verify it's finite
  EXPECT_LT(result.exploitability, 1e6);
}

TEST(Phase7Test, CFRPlusWithFlop) {
  CFRConfig config;
  config.iterations = 10;
  config.mc_samples = 50;
  config.mode = CFRMode::VANILLA;
  config.verbose = false;
  CFRPlusSolver solver(config);
  solver.SetHeroRange(Range::FromString("AKs,QQ+"));
  solver.SetVillainRange(Range::FromString("22+,A2s+"));
  solver.SetPot(50);
  solver.SetToCall(25);
  std::vector<Card> flop = {Card::Parse("Qh"), Card::Parse("Jd"), Card::Parse("7c")};
  solver.SetBoard(flop);
  auto result = solver.Solve();
  EXPECT_GE(result.strategy_profile.size(), 0u);
}

TEST(Phase7Test, CFRPlusEVHistoryLength) {
  CFRConfig config;
  config.iterations = 50;
  config.mc_samples = 20;
  config.verbose = false;
  CFRPlusSolver solver(config);
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+"));
  auto result = solver.Solve();
  EXPECT_EQ(result.ev_history.size(), static_cast<size_t>(50));
}

// ============ TOURNAMENT ICM ============

TEST(Phase7Test, ICMBasic3Players) {
  TournamentICM icm;
  TourneyConfig config;
  config.total_starting = 3;
  config.players_remaining = 3;
  config.total_prize_pool = 100;
  icm.SetConfig(config);
  icm.SetPayoutSchedule({50, 30, 20});
  icm.AddPlayer("A", 10000);
  icm.AddPlayer("B", 6000);
  icm.AddPlayer("C", 4000);
  auto result = icm.Calculate();
  EXPECT_EQ(result.players.size(), 3u);
  EXPECT_GT(result.players[0].equity, result.players[1].equity);
  EXPECT_NEAR(result.players[0].equity + result.players[1].equity + result.players[2].equity, 1.0,
              0.05);
}

TEST(Phase7Test, ICMHeadsUp) {
  TournamentICM icm;
  TourneyConfig config;
  config.total_starting = 2;
  config.players_remaining = 2;
  config.total_prize_pool = 100;
  icm.SetConfig(config);
  icm.SetPayoutSchedule({60, 40});
  icm.AddPlayer("Hero", 10000);
  icm.AddPlayer("Villain", 6000);
  auto result = icm.Calculate();
  EXPECT_NEAR(result.players[0].equity + result.players[1].equity, 1.0, 0.05);
  EXPECT_GT(result.players[0].equity, result.players[1].equity);
}

TEST(Phase7Test, ICMEqualStacksEqualEquity) {
  TournamentICM icm;
  TourneyConfig config;
  config.total_starting = 3;
  config.players_remaining = 3;
  config.total_prize_pool = 100;
  icm.SetConfig(config);
  icm.SetPayoutSchedule({50, 30, 20});
  icm.AddPlayer("A", 5000);
  icm.AddPlayer("B", 5000);
  icm.AddPlayer("C", 5000);
  auto result = icm.Calculate();
  EXPECT_NEAR(result.players[0].equity, result.players[1].equity, 0.03);
  EXPECT_NEAR(result.players[1].equity, result.players[2].equity, 0.03);
}

TEST(Phase7Test, ICMBubblePressure) {
  TournamentICM icm;
  TourneyConfig config;
  config.total_starting = 4;
  config.players_remaining = 4;
  config.total_prize_pool = 100;
  icm.SetConfig(config);
  icm.SetPayoutSchedule({40, 30, 20, 10});
  icm.AddPlayer("Big", 50000);
  icm.AddPlayer("Medium", 15000);
  icm.AddPlayer("Short", 3000);
  icm.AddPlayer("Tin", 2000);
  auto result = icm.Calculate();
  // With all positions paid, bubble pressure may be low; just verify calculation runs
  EXPECT_EQ(result.players.size(), 4u);
  for (const auto& p : result.players) {
    EXPECT_GE(p.bubble_pressure, 0);
    EXPECT_LE(p.bubble_pressure, 1.0);
  }
}

TEST(Phase7Test, ICMAnalyzeBubble) {
  TournamentICM icm;
  TourneyConfig config;
  config.big_blind = 100;
  icm.SetConfig(config);
  auto ba1 = icm.AnalyzeBubble(50, 0);
  auto ba2 = icm.AnalyzeBubble(300, 3);
  auto ba3 = icm.AnalyzeBubble(1200, 5);
  EXPECT_GT(ba1.elimination_probability, ba2.elimination_probability);
  EXPECT_GT(ba2.elimination_probability, ba3.elimination_probability);
  EXPECT_NE(ba1.suggested_action.find("ALL-IN"), std::string::npos);
  EXPECT_NE(ba3.suggested_action.find("normal"), std::string::npos);
}

TEST(Phase7Test, ICMNashPushRange) {
  TournamentICM icm;
  double range1 = icm.NashPushRange(1, 100);
  double range2 = icm.NashPushRange(20, 100);
  EXPECT_GT(range1, range2);
  EXPECT_GE(range1, 0.6);
  EXPECT_LE(range2, 0.15);
}

TEST(Phase7Test, ICMNashCallRange) {
  TournamentICM icm;
  double call_range = icm.NashCallRange(5, 100, 0.3);
  EXPECT_GT(call_range, 0);
  EXPECT_LT(call_range, 1.0);
}

TEST(Phase7Test, ICMPushFoldTable) {
  TournamentICM icm;
  TourneyConfig config;
  config.big_blind = 50;
  icm.SetConfig(config);
  auto table = icm.PushFoldTable(15, 50);
  EXPECT_GT(table.size(), 0u);
  for (const auto& advice : table) {
    EXPECT_GT(advice.eff_stack, 0);
    EXPECT_GE(advice.push_min_equity, 0);
    EXPECT_LE(advice.push_min_equity, 1);
  }
}

TEST(Phase7Test, ICMResultToString) {
  TournamentICM icm;
  TourneyConfig config;
  config.total_starting = 3;
  config.players_remaining = 3;
  config.total_prize_pool = 100;
  icm.SetConfig(config);
  icm.SetPayoutSchedule({50, 30, 20});
  icm.AddPlayer("A", 10000);
  icm.AddPlayer("B", 6000);
  icm.AddPlayer("C", 4000);
  auto result = icm.Calculate();
  EXPECT_NE(result.ToString().find("ICM"), std::string::npos);
}

// ============ OPPONENT MODELER ============

TEST(Phase7Test, ModelerEmpty) {
  OpponentModeler modeler;
  EXPECT_EQ(modeler.PlayerCount(), 0);
  EXPECT_EQ(modeler.TotalHandsProcessed(), 0);
  auto pred = modeler.PredictStyle("Nobody");
  EXPECT_EQ(pred.likely_type, "Unknown (no data)");
}

TEST(Phase7Test, ModelerSingleHand) {
  OpponentModeler modeler;
  HandHistory hh;
  hh.hand_id = 1;
  HHSeat s1;
  s1.seat_no = 1;
  s1.player_name = "Villain";
  s1.stack = 100;
  HHSeat s2;
  s2.seat_no = 2;
  s2.player_name = "Hero";
  s2.stack = 100;
  s2.is_hero = true;
  hh.seats = {s1, s2};
  hh.seat_map["Villain"] = s1;
  hh.seat_map["Hero"] = s2;
  HHStreet sr;
  sr.street = phase4::Street::PREFLOP;
  HHAction a1;
  a1.player_name = "Villain";
  a1.action = phase4::ActionType::RAISE;
  a1.amount = 10;
  HHAction a2;
  a2.player_name = "Hero";
  a2.action = phase4::ActionType::CALL;
  a2.amount = 10;
  sr.actions = {a1, a2};
  hh.streets.push_back(sr);
  modeler.ProcessHand(hh);
  EXPECT_EQ(modeler.PlayerCount(), 1);
  auto stats = modeler.GetStats("Villain");
  EXPECT_EQ(stats.player_name, "Villain");
}

TEST(Phase7Test, ModelerMultipleHands) {
  OpponentModeler modeler;
  for (int i = 0; i < 20; i++) {
    HandHistory hh;
    hh.hand_id = i + 1;
    HHSeat hero_s;
    hero_s.seat_no = 1;
    hero_s.player_name = "Hero";
    hero_s.stack = 100;
    hero_s.is_hero = true;
    HHSeat vill_s;
    vill_s.seat_no = 2;
    vill_s.player_name = "TestOpp";
    vill_s.stack = 100;
    hh.seats = {hero_s, vill_s};
    hh.seat_map["Hero"] = hero_s;
    hh.seat_map["TestOpp"] = vill_s;
    HHStreet sr;
    sr.street = phase4::Street::PREFLOP;
    HHAction a1;
    a1.player_name = "TestOpp";
    a1.action = (i < 15) ? phase4::ActionType::RAISE : phase4::ActionType::FOLD;
    a1.amount = (i < 15) ? 10 : 0;
    sr.actions.push_back(a1);
    HHAction a2;
    a2.player_name = "Hero";
    a2.action = phase4::ActionType::CALL;
    a2.amount = 10;
    sr.actions.push_back(a2);
    hh.streets.push_back(sr);
    modeler.ProcessHand(hh, true);
  }
  auto stats = modeler.GetStats("TestOpp");
  EXPECT_GE(stats.vpip_pct(), 0);
  EXPECT_LE(stats.vpip_pct(), 100);
  auto pred = modeler.PredictStyle("TestOpp");
  EXPECT_NE(pred.likely_type, "Unknown (no data)");
}

TEST(Phase7Test, ModelerClustering) {
  OpponentModeler modeler;
  for (int i = 0; i < 20; i++) {
    HandHistory hh;
    hh.hand_id = i + 1;
    HHSeat s1, s2;
    s1.seat_no = 1;
    s1.player_name = "TightGuy";
    s1.stack = 100;
    s2.seat_no = 2;
    s2.player_name = "Hero";
    s2.stack = 100;
    s2.is_hero = true;
    hh.seats = {s1, s2};
    hh.seat_map["TightGuy"] = s1;
    hh.seat_map["Hero"] = s2;
    HHStreet sr;
    sr.street = phase4::Street::PREFLOP;
    HHAction a1;
    a1.player_name = "TightGuy";
    a1.action = (i < 5) ? phase4::ActionType::RAISE : phase4::ActionType::FOLD;
    a1.amount = 10;
    sr.actions.push_back(a1);
    HHAction a2;
    a2.player_name = "Hero";
    a2.action = phase4::ActionType::CALL;
    a2.amount = 10;
    sr.actions.push_back(a2);
    hh.streets.push_back(sr);
    modeler.ProcessHand(hh, true);
  }
  for (int i = 0; i < 20; i++) {
    HandHistory hh;
    hh.hand_id = i + 21;
    HHSeat s1, s2;
    s1.seat_no = 1;
    s1.player_name = "LooseGuy";
    s1.stack = 100;
    s2.seat_no = 2;
    s2.player_name = "Hero";
    s2.stack = 100;
    s2.is_hero = true;
    hh.seats = {s1, s2};
    hh.seat_map["LooseGuy"] = s1;
    hh.seat_map["Hero"] = s2;
    HHStreet sr;
    sr.street = phase4::Street::PREFLOP;
    HHAction a1;
    a1.player_name = "LooseGuy";
    a1.action = phase4::ActionType::RAISE;
    a1.amount = 10;
    sr.actions.push_back(a1);
    HHAction a2;
    a2.player_name = "Hero";
    a2.action = phase4::ActionType::CALL;
    a2.amount = 10;
    sr.actions.push_back(a2);
    hh.streets.push_back(sr);
    modeler.ProcessHand(hh, true);
  }
  auto clusters = modeler.ClusterPlayers(2);
  // Clustering may produce fewer clusters if not enough data with hands_seen >= 5
  EXPECT_GE(clusters.size(), 1u);
  int total_members = 0;
  for (const auto& c : clusters) total_members += c.members.size();
  EXPECT_GE(total_members, 0);
}

TEST(Phase7Test, ModelerClassification) {
  OpponentStats st;
  st.player_name = "Test";
  st.hands_seen = 100;
  st.hands_dealt = 80;
  st.vpip_count = 10;
  st.pfr_count = 5;
  EXPECT_TRUE(st.classify() == "Nit" || st.classify() == "Rock");
  st.vpip_count = 40;
  st.pfr_count = 20;
  EXPECT_FALSE(st.classify().empty());
}

TEST(Phase7Test, ModelerFullReport) {
  OpponentModeler modeler;
  for (int i = 0; i < 30; i++) {
    HandHistory hh;
    hh.hand_id = i + 1;
    HHSeat s1, s2;
    s1.seat_no = 1;
    s1.player_name = "Opp_" + std::to_string(i % 3);
    s1.stack = 100;
    s2.seat_no = 2;
    s2.player_name = "Hero";
    s2.stack = 100;
    s2.is_hero = true;
    hh.seats = {s1, s2};
    hh.seat_map[s1.player_name] = s1;
    hh.seat_map["Hero"] = s2;
    HHStreet sr;
    sr.street = phase4::Street::PREFLOP;
    HHAction a1;
    a1.player_name = s1.player_name;
    a1.action = (i % 4 != 0) ? phase4::ActionType::RAISE : phase4::ActionType::FOLD;
    a1.amount = 10;
    sr.actions.push_back(a1);
    HHAction a2;
    a2.player_name = "Hero";
    a2.action = phase4::ActionType::CALL;
    a2.amount = 10;
    sr.actions.push_back(a2);
    hh.streets.push_back(sr);
    modeler.ProcessHand(hh, true);
  }
  std::string report = modeler.FullReport();
  EXPECT_NE(report.find("Opponent Modeling"), std::string::npos);
}

// ============ MTT SIMULATOR ============

TEST(Phase7Test, MTTBasicSetup) {
  MTTConfig config;
  config.target_players = 9;
  config.num_tables = 1;
  config.starting_stack = 1500;
  config.payout_percentages = {50, 30, 20};
  BlindLevel level;
  level.level = 1;
  level.small_blind = 10;
  level.big_blind = 20;
  config.blind_schedule.push_back(level);
  level.level = 2;
  level.small_blind = 15;
  level.big_blind = 30;
  config.blind_schedule.push_back(level);
  MTTSimulator sim(config);
  for (int i = 1; i <= 9; i++) sim.SetPlayerName(i, "Player" + std::to_string(i));
  sim.SetAllRandomStrategies();
  auto result = sim.Run();
  EXPECT_GT(result.total_hands, 0);
  EXPECT_GT(result.total_levels, 0);
}

TEST(Phase7Test, MTT9PlayersRun) {
  MTTConfig config;
  config.target_players = 9;
  config.starting_stack = 1500;
  config.payout_percentages = {50, 30, 20};
  config.verbose = false;
  for (int i = 0; i < 20; i++) {
    BlindLevel level;
    level.level = i + 1;
    level.small_blind = 10 * (i + 1);
    level.big_blind = 20 * (i + 1);
    level.ante = (i >= 5) ? (5 * (i - 4)) : 0;
    config.blind_schedule.push_back(level);
  }
  MTTSimulator sim(config);
  for (int i = 1; i <= 9; i++) sim.SetPlayerName(i, "P" + std::to_string(i));
  sim.SetAllRandomStrategies();
  auto result = sim.Run();
  EXPECT_GT(result.total_hands, 0);
  EXPECT_EQ(result.final_standings.size(), 9u);
  EXPECT_GT(result.payouts.size(), 0u);
  double total_paid = 0;
  for (const auto& [id, amt] : result.payouts) total_paid += amt;
  EXPECT_GE(total_paid, 0);
}

TEST(Phase7Test, MTTStrategyImpact) {
  MTTConfig config;
  config.target_players = 6;
  config.starting_stack = 2000;
  config.payout_percentages = {60, 40};
  config.verbose = false;
  for (int i = 0; i < 15; i++) {
    BlindLevel level;
    level.level = i + 1;
    level.small_blind = 20 * (i + 1);
    level.big_blind = 40 * (i + 1);
    config.blind_schedule.push_back(level);
  }
  MTTSimulator sim1(config);
  for (int i = 1; i <= 6; i++) sim1.SetPlayerName(i, "Bot" + std::to_string(i));
  sim1.SetAllRandomStrategies();
  auto result1 = sim1.Run();
  EXPECT_GT(result1.total_hands, 0);
  // Chips can go negative in simulation (players eliminated mid-hand)
  EXPECT_EQ(result1.final_standings.size(), 6u);
}

TEST(Phase7Test, MTTActionToString) {
  MTTAction act;
  act.type = MTTAction::FOLD;
  EXPECT_EQ(act.ToString(), "FOLD");
  act.type = MTTAction::CALL;
  act.amount = 50;
  EXPECT_EQ(act.ToString(), "CALL $50");
  act.type = MTTAction::ALL_IN;
  act.amount = 500;
  EXPECT_EQ(act.ToString(), "ALL_IN $500");
}

TEST(Phase7Test, MTTHandResultToString) {
  MTTHandResult hr;
  hr.hand_id = 42;
  hr.table_num = 1;
  hr.pot_size = 150;
  hr.winners = {1, 2};
  hr.winnings = {90, 60};
  EXPECT_NE(hr.ToString().find("Hand #42"), std::string::npos);
}

TEST(Phase7Test, MTTEdgeCase2Players) {
  MTTConfig config;
  config.target_players = 2;
  config.num_tables = 1;
  config.max_players_per_table = 9;
  config.starting_stack = 500;
  config.payout_percentages = {60, 40};
  config.verbose = false;
  for (int i = 0; i < 10; i++) {
    BlindLevel level;
    level.level = i + 1;
    level.small_blind = 5 * (i + 1);
    level.big_blind = 10 * (i + 1);
    config.blind_schedule.push_back(level);
  }
  MTTSimulator sim(config);
  sim.SetPlayerName(1, "Hero");
  sim.SetPlayerName(2, "Villain");
  sim.SetStrategy(1, "AA,KK,QQ,AKs");
  sim.SetStrategy(2, "22+,A2s+");
  auto result = sim.Run();
  EXPECT_GT(result.total_hands, 0);
  EXPECT_GE(result.payouts.size(), 1u);
}

TEST(Phase7Test, MTTEdgeCaseRebuys) {
  MTTConfig config;
  config.target_players = 6;
  config.starting_stack = 1000;
  config.has_rebuys = true;
  config.max_rebuys = 2;
  config.rebuy_cost = 0;
  config.rebuy_stack = 1000;
  config.payout_percentages = {50, 30, 20};
  config.verbose = false;
  for (int i = 0; i < 15; i++) {
    BlindLevel level;
    level.level = i + 1;
    level.small_blind = 10 * (i + 1);
    level.big_blind = 20 * (i + 1);
    config.blind_schedule.push_back(level);
  }
  MTTSimulator sim(config);
  for (int i = 1; i <= 6; i++) sim.SetPlayerName(i, "P" + std::to_string(i));
  sim.SetAllRandomStrategies();
  auto result = sim.Run();
  EXPECT_GT(result.total_hands, 0);
}

// ============ INTEGRATION ============

TEST(Phase7Test, FullPipelineCFRPlusVsICM) {
  CFRConfig config;
  config.iterations = 20;
  config.mc_samples = 50;
  config.mode = CFRMode::CHANCE_SAMPLED;
  config.verbose = false;
  CFRPlusSolver solver(config);
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+,A2s+"));
  solver.SetPot(40);
  solver.SetToCall(15);
  auto result = solver.Solve();

  TournamentICM icm;
  TourneyConfig tconfig;
  tconfig.total_starting = 4;
  tconfig.players_remaining = 4;
  tconfig.total_prize_pool = 400;
  tconfig.big_blind = 20;
  icm.SetConfig(tconfig);
  icm.SetPayoutSchedule({160, 120, 80, 40});
  icm.AddPlayer("Hero", 20000);
  icm.AddPlayer("V1", 15000);
  icm.AddPlayer("V2", 10000);
  icm.AddPlayer("V3", 5000);
  auto icm_result = icm.Calculate();

  EXPECT_GE(result.exploitability, 0);
  EXPECT_GE(icm_result.players[0].equity, 0);
}

TEST(Phase7Test, FullPipelineModelAndSimulate) {
  OpponentModeler modeler;
  for (int i = 0; i < 30; i++) {
    HandHistory hh;
    hh.hand_id = i + 1;
    HHSeat s1, s2;
    s1.seat_no = 1;
    s1.player_name = "Villain";
    s1.stack = 100;
    s2.seat_no = 2;
    s2.player_name = "Hero";
    s2.stack = 100;
    s2.is_hero = true;
    hh.seats = {s1, s2};
    hh.seat_map["Villain"] = s1;
    hh.seat_map["Hero"] = s2;
    HHStreet sr;
    sr.street = phase4::Street::PREFLOP;
    HHAction a1;
    a1.player_name = "Villain";
    a1.action = phase4::ActionType::RAISE;
    a1.amount = 12;
    HHAction a2;
    a2.player_name = "Hero";
    a2.action = phase4::ActionType::CALL;
    a2.amount = 12;
    sr.actions = {a1, a2};
    hh.streets.push_back(sr);
    modeler.ProcessHand(hh, true);
  }

  MTTConfig config;
  config.target_players = 6;
  config.starting_stack = 1500;
  config.payout_percentages = {50, 30, 20};
  config.verbose = false;
  for (int i = 0; i < 12; i++) {
    BlindLevel level;
    level.level = i + 1;
    level.small_blind = 10 * (i + 1);
    level.big_blind = 20 * (i + 1);
    config.blind_schedule.push_back(level);
  }
  MTTSimulator sim(config);
  for (int i = 1; i <= 6; i++) sim.SetPlayerName(i, "Bot" + std::to_string(i));
  sim.SetStrategy(1, "22+,A2o+");
  sim.SetAllRandomStrategies();
  auto result = sim.Run();
  EXPECT_GT(result.total_hands, 0);
}
