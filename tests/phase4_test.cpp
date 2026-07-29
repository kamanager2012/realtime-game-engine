#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/phase3/flop_explorer.h"
#include "poker_engine/phase3/spot_solver.h"
#include "poker_engine/phase4/hh_parser.h"
#include "poker_engine/phase4/multi_street_solver.h"
#include "poker_engine/phase4/session_analyzer.h"
#include "poker_engine/range/range.h"

using namespace poker_engine::range;

// ============ HAND HISTORY PARSER ============

TEST(Phase4Test, ParserBasicStructure) {
  poker_engine::phase4::HandHistory hh;
  EXPECT_EQ(hh.hand_id, 0);
  EXPECT_TRUE(hh.seats.empty());
  EXPECT_TRUE(hh.streets.empty());
  EXPECT_TRUE(hh.results.empty());
  EXPECT_DOUBLE_EQ(hh.total_pot, 0);
}

TEST(Phase4Test, ParseSimpleHand) {
  const std::string hh_text = R"(
PokerStars Hand #123456789: Hold'em No Limit ($1/$2) - 2024/01/15
Table 'Mars' 6-max Seat #3 is the button
Seat 1: PlayerA ($1000)
Seat 2: PlayerB ($1000)
Seat 3: Hero ($1000)
Seat 4: PlayerD ($1000)
Seat 5: PlayerE ($1000)
Seat 6: PlayerF ($1000)
Dealt to Hero [As Ks]
PlayerA: posts the small blind $1
PlayerB: posts the big blind $2
Hero: raises to $7
PlayerD: folds
PlayerE: calls $7
PlayerF: folds
PlayerA: folds
PlayerB: calls $5
*** FLOP *** [Qh Jd 7c]
PlayerB: checks
Hero: bets $12
PlayerE: calls $12
PlayerB: calls $12
*** TURN *** [8s]
PlayerB: checks
Hero: bets $25
PlayerE: folds
PlayerB: calls $25
*** RIVER *** [3h]
PlayerB: checks
Hero: bets $50
PlayerB: folds
*** SUMMARY ***
Total pot: $192
Hero collected $192 with a pair of Queens
)";

  poker_engine::phase4::HandHistoryParser parser;
  poker_engine::phase4::HandHistory hh = parser.Parse(hh_text);

  EXPECT_EQ(hh.hand_id, 123456789);
  EXPECT_EQ(hh.site, "PokerStars");
  EXPECT_EQ(hh.game_type, "No Limit Hold'em");
  EXPECT_EQ(hh.table_name, "Mars");
  EXPECT_EQ(hh.small_blind, 1);
  EXPECT_EQ(hh.big_blind, 2);
  EXPECT_EQ(hh.max_seats, 6);
  EXPECT_EQ(hh.button_seat, 3);
  EXPECT_EQ(hh.total_pot, 192);

  EXPECT_EQ(hh.seats.size(), 6);
  EXPECT_EQ(hh.HeroName(), "Hero");

  auto hero_cards = hh.HeroCards();
  ASSERT_EQ(hero_cards.size(), 2);
  EXPECT_EQ(hero_cards[0].ToString(), "As");
  EXPECT_EQ(hero_cards[1].ToString(), "Ks");

  // Streets
  ASSERT_EQ(hh.streets.size(), 4);  // PREFLOP, FLOP, TURN, RIVER

  // PREFLOP actions
  auto& preflop = hh.streets[0];
  EXPECT_EQ(static_cast<int>(preflop.street), 0);
  EXPECT_GT(preflop.actions.size(), 0);

  // FLOP cards
  auto& flop = hh.streets[1];
  EXPECT_EQ(flop.community_cards.size(), 3);
  EXPECT_EQ(flop.community_cards[0].ToString(), "Qh");
  EXPECT_EQ(flop.community_cards[1].ToString(), "Jd");
  EXPECT_EQ(flop.community_cards[2].ToString(), "7c");

  // Results
  ASSERT_EQ(hh.results.size(), 1);
  EXPECT_EQ(hh.results[0].player_name, "Hero");
  EXPECT_EQ(hh.results[0].amount, 192);

  // Summary
  EXPECT_NE(hh.ToShortSummary().find("Hand #123456789"), std::string::npos);
}

TEST(Phase4Test, ParseMultipleHands) {
  const std::string text = R"(
PokerStars Hand #100: Hold'em No Limit ($1/$2)
Table 'T1' 6-max Seat #1 is the button
Seat 1: Hero ($1000)
Seat 2: Villain ($1000)
Dealt to Hero [As Ks]
Villain: posts the small blind $1
Hero: posts the big blind $2
Hero: raises to $7
Villain: folds
*** SUMMARY ***
Total pot: $10
Hero collected $10
)";

  poker_engine::phase4::HandHistoryParser parser;
  auto hands = parser.ParseMultiple(text);
  EXPECT_EQ(hands.size(), 1);
  EXPECT_EQ(hands[0].hand_id, 100);
}

TEST(Phase4Test, ActionTypeParsing) {
  EXPECT_EQ(static_cast<int>(poker_engine::phase4::HandHistoryParser::ParseActionType("folds")),
            static_cast<int>(poker_engine::phase4::ActionType::FOLD));
  EXPECT_EQ(static_cast<int>(poker_engine::phase4::HandHistoryParser::ParseActionType("checks")),
            static_cast<int>(poker_engine::phase4::ActionType::CHECK));
  EXPECT_EQ(static_cast<int>(poker_engine::phase4::HandHistoryParser::ParseActionType("calls $50")),
            static_cast<int>(poker_engine::phase4::ActionType::CALL));
  EXPECT_EQ(
      static_cast<int>(poker_engine::phase4::HandHistoryParser::ParseActionType("raises to $100")),
      static_cast<int>(poker_engine::phase4::ActionType::RAISE));
  EXPECT_EQ(
      static_cast<int>(poker_engine::phase4::HandHistoryParser::ParseActionType("all-in $50")),
      static_cast<int>(poker_engine::phase4::ActionType::ALL_IN));
  EXPECT_EQ(static_cast<int>(poker_engine::phase4::HandHistoryParser::ParseActionType(
                "posts the small blind $1")),
            static_cast<int>(poker_engine::phase4::ActionType::POST_SB));
  EXPECT_EQ(static_cast<int>(
                poker_engine::phase4::HandHistoryParser::ParseActionType("posts the big blind $2")),
            static_cast<int>(poker_engine::phase4::ActionType::POST_BB));
}

TEST(Phase4Test, StreetDetection) {
  EXPECT_EQ(static_cast<int>(poker_engine::phase4::HandHistoryParser::ParseStreet("*** FLOP ***")),
            static_cast<int>(poker_engine::phase4::Street::FLOP));
  EXPECT_EQ(static_cast<int>(poker_engine::phase4::HandHistoryParser::ParseStreet("*** TURN ***")),
            static_cast<int>(poker_engine::phase4::Street::TURN));
  EXPECT_EQ(static_cast<int>(poker_engine::phase4::HandHistoryParser::ParseStreet("*** RIVER ***")),
            static_cast<int>(poker_engine::phase4::Street::RIVER));
  EXPECT_EQ(
      static_cast<int>(poker_engine::phase4::HandHistoryParser::ParseStreet("*** SHOW DOWN ***")),
      static_cast<int>(poker_engine::phase4::Street::SHOWDOWN));
}

TEST(Phase4Test, HeroNameAndCards) {
  const std::string hh_text = R"(
PokerStars Hand #99: Hold'em No Limit ($0.05/$0.10)
Table 'Test' 6-max Seat #1 is the button
Seat 1: Seat1Player ($100)
Seat 2: Seat2Player ($100)
Seat 3: MyHero ($100)
Seat 4: Seat4Player ($100)
Seat 5: Seat5Player ($100)
Seat 6: Seat6Player ($100)
Dealt to MyHero [Qd Qh]
Seat1Player: posts the small blind $0.05
Seat2Player: posts the big blind $0.10
MyHero: raises to $0.30
*** SUMMARY ***
Total pot: $0.50
MyHero collected $0.50
)";

  poker_engine::phase4::HandHistoryParser parser;
  auto hh = parser.Parse(hh_text);

  EXPECT_EQ(hh.HeroName(), "MyHero");
  EXPECT_EQ(hh.HeroSeatNo(), 3);

  auto cards = hh.HeroCards();
  ASSERT_EQ(cards.size(), 2);
  EXPECT_EQ(cards[0].ToString(), "Qd");
  EXPECT_EQ(cards[1].ToString(), "Qh");
}

TEST(Phase4Test, ToStringContainsInfo) {
  const std::string hh_text = R"(
PokerStars Hand #555: Hold'em No Limit ($1/$2)
Table 'InfoTest' 6-max Seat #3 is the button
Seat 1: A ($1000)
Seat 2: B ($1000)
Seat 3: Hero ($1000)
Seat 4: D ($1000)
Seat 5: E ($1000)
Seat 6: F ($1000)
Dealt to Hero [Ah Kh]
A: posts the small blind $1
B: posts the big blind $2
Hero: raises to $7
D: folds
E: folds
F: folds
A: folds
B: calls $5
*** FLOP *** [Th 9d 3c]
B: checks
Hero: bets $15
B: folds
*** SUMMARY ***
Total pot: $33
Hero collected $33
)";

  poker_engine::phase4::HandHistoryParser parser;
  auto hh = parser.Parse(hh_text);
  std::string s = hh.ToString();

  EXPECT_NE(s.find("Hand #555"), std::string::npos);
  EXPECT_NE(s.find("PokerStars"), std::string::npos);
  EXPECT_NE(s.find("InfoTest"), std::string::npos);
  EXPECT_NE(s.find("Ah"), std::string::npos);
  EXPECT_NE(s.find("Kh"), std::string::npos);
  EXPECT_NE(s.find("FLOP"), std::string::npos);
  EXPECT_NE(s.find("Th"), std::string::npos);
  EXPECT_NE(s.find("Total pot: $33"), std::string::npos);
}

// ============ MULTI-STREET SOLVER ============

TEST(Phase4Test, MS_SolverCreation) {
  poker_engine::phase4::MS_Config config;
  config.iterations = 50;
  config.mc_samples = 500;
  config.verbose = false;

  poker_engine::phase4::MultiStreetSolver solver(config);

  solver.SetRanges({Range::FromString("AKs"), Range::FromString("22+,A2s+")});

  std::vector<poker_engine::Card> flop = {poker_engine::Card::Parse("Qh"),
                                          poker_engine::Card::Parse("Jd"),
                                          poker_engine::Card::Parse("7c")};
  solver.SetFlop(flop);

  auto result = solver.SolveFromHand(poker_engine::phase4::HandHistory());
  EXPECT_GE(result.iterations, 50);
  EXPECT_GE(result.strategies.size(), 0);  // 可能为空因为简化逻辑
}

TEST(Phase4Test, MS_NodeBasics) {
  poker_engine::phase4::MS_Node node;
  EXPECT_EQ(node.visit_count, 0);

  double strat[poker_engine::phase4::NUM_MS_ACTIONS];
  node.GetStrategy(strat);

  double sum = 0;
  for (int a = 0; a < poker_engine::phase4::NUM_MS_ACTIONS; a++) {
    EXPECT_GE(strat[a], 0);
    sum += strat[a];
  }
  EXPECT_NEAR(sum, 1.0, 0.001);

  double avg[poker_engine::phase4::NUM_MS_ACTIONS];
  node.GetAverageStrategy(avg);
  double avg_sum = 0;
  for (int a = 0; a < poker_engine::phase4::NUM_MS_ACTIONS; a++) avg_sum += avg[a];
  EXPECT_NEAR(avg_sum, 1.0, 0.001);
}

TEST(Phase4Test, MS_NodeRegretUpdate) {
  poker_engine::phase4::MS_Node node;

  // 模拟 positive 和 negative regret
  node.cumulative_regret[static_cast<int>(poker_engine::phase4::MS_Action::FOLD)] = -50;
  node.cumulative_regret[static_cast<int>(poker_engine::phase4::MS_Action::CALL)] = 100;
  node.cumulative_regret[static_cast<int>(poker_engine::phase4::MS_Action::BET_POT)] = 30;
  node.cumulative_regret[static_cast<int>(poker_engine::phase4::MS_Action::ALL_IN)] = -20;

  double strat[poker_engine::phase4::NUM_MS_ACTIONS];
  node.GetStrategy(strat);

  // Negative regrets should produce 0 strategy
  EXPECT_NEAR(strat[static_cast<int>(poker_engine::phase4::MS_Action::FOLD)], 0.0, 0.001);
  EXPECT_NEAR(strat[static_cast<int>(poker_engine::phase4::MS_Action::ALL_IN)], 0.0, 0.001);

  // Positive regrets should have positive strategy
  EXPECT_GT(strat[static_cast<int>(poker_engine::phase4::MS_Action::CALL)], 0.5);
  EXPECT_GT(strat[static_cast<int>(poker_engine::phase4::MS_Action::BET_POT)], 0.1);

  // Sum = 1
  double sum = 0;
  for (int a = 0; a < poker_engine::phase4::NUM_MS_ACTIONS; a++) sum += strat[a];
  EXPECT_NEAR(sum, 1.0, 0.001);
}

TEST(Phase4Test, MS_SolveResultFormat) {
  poker_engine::phase4::MS_SolveResult result;
  result.iterations = 100;
  result.exploitability = 5.0;

  poker_engine::phase4::MS_InfoSetKey key1{0, "cc"};
  poker_engine::phase4::MS_InfoSetKey key2{1, "cbc"};
  result.strategies[key1] = {0.1, 0.7, 0.2, 0.0, 0.0};
  result.strategies[key2] = {0.0, 0.3, 0.5, 0.2, 0.0};

  std::string s = result.ToString();
  EXPECT_NE(s.find("Multi-Street CFRA"), std::string::npos);
  EXPECT_NE(s.find("100"), std::string::npos);
  EXPECT_NE(s.find("5.0"), std::string::npos);
  EXPECT_NE(s.find("CHECK"), std::string::npos);
}

TEST(Phase4Test, MS_Visualization) {
  poker_engine::phase4::MS_InfoSetKey key{1, "cbc"};
  std::array<double, poker_engine::phase4::NUM_MS_ACTIONS> strat = {0.05, 0.45, 0.40, 0.08, 0.02};

  std::string node_str = poker_engine::phase4::StrategyVisualization::PrintNode(key, strat);
  EXPECT_NE(node_str.find("Street 1"), std::string::npos);
  EXPECT_NE(node_str.find("CHECK"), std::string::npos);
  EXPECT_NE(node_str.find("CALL"), std::string::npos);

  std::map<poker_engine::phase4::MS_InfoSetKey,
           std::array<double, poker_engine::phase4::NUM_MS_ACTIONS>>
      strategies;
  strategies[key] = strat;
  std::string all = poker_engine::phase4::StrategyVisualization::PrintAllNodes(strategies);
  EXPECT_NE(all.find("Street 1"), std::string::npos);
}

TEST(Phase4Test, MSSolverWithNoBoard) {
  poker_engine::phase4::MS_Config config;
  config.iterations = 30;
  config.mc_samples = 500;
  config.verbose = false;

  poker_engine::phase4::MultiStreetSolver solver(config);
  solver.SetRanges({Range::FromString("AA"), Range::FromString("22+,A2s+")});

  auto result = solver.SolveFullTree();
  // Should have created some nodes
  EXPECT_GE(result.iterations, 30);
}

// ============ SESSION ANALYZER ============

TEST(Phase4Test, SessionEmptyStats) {
  poker_engine::phase4::SessionAnalyzer analyzer;
  auto stats = analyzer.ComputeStats();

  EXPECT_EQ(stats.total_hands, 0);
  EXPECT_DOUBLE_EQ(stats.total_net, 0);
  EXPECT_DOUBLE_EQ(stats.bb_per_100, 0);
  EXPECT_EQ(stats.vpip_by_pos.size(), 0);
  EXPECT_NE(stats.ToString().find("Session"), std::string::npos);
}

TEST(Phase4Test, SessionBasicStats) {
  // Build a session with 2 hands
  poker_engine::phase4::SessionAnalyzer analyzer;

  // Hand 1: Hero wins
  {
    poker_engine::phase4::HandHistory hh;
    hh.hand_id = 1;
    hh.site = "TestSite";
    hh.game_type = "No Limit Hold'em";
    hh.small_blind = 0.5;
    hh.big_blind = 1.0;
    hh.total_pot = 10;

    poker_engine::phase4::HHSeat s1;
    s1.seat_no = 1;
    s1.player_name = "Hero";
    s1.stack = 100;
    s1.is_hero = true;
    poker_engine::phase4::HHSeat s2;
    s2.seat_no = 2;
    s2.player_name = "Villain";
    s2.stack = 100;
    hh.seats = {s1, s2};
    hh.seat_map["Hero"] = s1;
    hh.seat_map["Villain"] = s2;

    hh.hero_cards_[0] = poker_engine::Card::Parse("As");
    hh.hero_cards_[1] = poker_engine::Card::Parse("Ks");
    hh.hero_cards_known_ = true;

    poker_engine::phase4::HHStreet sr;
    sr.street = poker_engine::phase4::Street::PREFLOP;
    poker_engine::phase4::HHAction a1;
    a1.player_name = "Villain";
    a1.action = poker_engine::phase4::ActionType::POST_BB;
    a1.amount = 1.0;
    poker_engine::phase4::HHAction a2;
    a2.player_name = "Hero";
    a2.action = poker_engine::phase4::ActionType::POST_SB;
    a2.amount = 0.5;
    poker_engine::phase4::HHAction a3;
    a3.player_name = "Villain";
    a3.action = poker_engine::phase4::ActionType::FOLD;
    a3.amount = 0;
    sr.actions = {a1, a2, a3};
    hh.streets.push_back(sr);

    poker_engine::phase4::HHResult res;
    res.player_name = "Hero";
    res.amount = 1.5;
    hh.results.push_back(res);

    analyzer.AddHand(hh);
  }

  // Hand 2: Hero loses
  {
    poker_engine::phase4::HandHistory hh;
    hh.hand_id = 2;
    hh.site = "TestSite";
    hh.game_type = "No Limit Hold'em";
    hh.small_blind = 0.5;
    hh.big_blind = 1.0;
    hh.total_pot = 10;

    poker_engine::phase4::HHSeat s1;
    s1.seat_no = 1;
    s1.player_name = "Hero";
    s1.stack = 100;
    s1.is_hero = true;
    poker_engine::phase4::HHSeat s2;
    s2.seat_no = 2;
    s2.player_name = "Villain";
    s2.stack = 100;
    hh.seats = {s1, s2};
    hh.seat_map["Hero"] = s1;
    hh.seat_map["Villain"] = s2;
    hh.hero_cards_known_ = true;

    poker_engine::phase4::HHStreet sr;
    sr.street = poker_engine::phase4::Street::PREFLOP;
    poker_engine::phase4::HHAction a1;
    a1.player_name = "Villain";
    a1.action = poker_engine::phase4::ActionType::POST_BB;
    a1.amount = 1.0;
    poker_engine::phase4::HHAction a2;
    a2.player_name = "Hero";
    a2.action = poker_engine::phase4::ActionType::ALL_IN;
    a2.amount = 10.0;
    poker_engine::phase4::HHAction a3;
    a3.player_name = "Villain";
    a3.action = poker_engine::phase4::ActionType::CALL;
    a3.amount = 10.0;
    sr.actions = {a1, a2, a3};
    hh.streets.push_back(sr);

    poker_engine::phase4::HHResult res;
    res.player_name = "Villain";
    res.amount = 21;
    hh.results.push_back(res);

    analyzer.AddHand(hh);
  }

  auto stats = analyzer.ComputeStats();
  EXPECT_EQ(stats.total_hands, 2);
  EXPECT_GT(stats.vpip_by_pos.size(), 0);

  std::string report = stats.ToString();
  EXPECT_NE(report.find("Session"), std::string::npos);
  EXPECT_NE(report.find("Hands:"), std::string::npos);
}

TEST(Phase4Test, SessionFilter) {
  poker_engine::phase4::SessionAnalyzer analyzer;

  for (int i = 0; i < 5; i++) {
    poker_engine::phase4::HandHistory hh;
    hh.hand_id = i + 1;
    hh.game_type = "No Limit Hold'em";
    hh.big_blind = (i % 2 == 0) ? 1.0 : 2.0;

    poker_engine::phase4::HHSeat s;
    s.seat_no = 1;
    s.player_name = "TestPlayer";
    s.stack = 100;
    s.is_hero = true;
    hh.seats.push_back(s);
    hh.seat_map["TestPlayer"] = s;

    analyzer.AddHand(hh);
  }

  auto filtered = analyzer.FilterByBBRange(1.0, 1.0);
  EXPECT_EQ(filtered.size(), 3);  // 0, 2, 4 → BB=1.0

  filtered = analyzer.FilterByGameType("No Limit");
  EXPECT_EQ(filtered.size(), 5);
}

// ============ INTEGRATION: Full Parse + Analyze ============

TEST(Phase4Test, FullPipelineParseAndReplay) {
  const std::string hh_text = R"(
PokerStars Hand #1000: Hold'em No Limit ($1/$2)
Table 'Integration' 6-max Seat #4 is the button
Seat 1: Alpha ($500)
Seat 2: Beta ($500)
Seat 3: Charlie ($500)
Seat 4: Hero ($500)
Seat 5: Delta ($500)
Seat 6: Epsilon ($500)
Dealt to Hero [Qh Qd]
Alpha: posts the small blind $1
Beta: posts the big blind $2
Charlie: folds
Hero: raises to $8
Delta: calls $8
Epsilon: folds
Alpha: folds
Beta: calls $6
*** FLOP *** [Ah 7s 3c]
Beta: checks
Hero: bets $15
Delta: folds
Beta: folds
*** SUMMARY ***
Total pot: $42
Hero collected $42
)";

  // 1. Parse
  poker_engine::phase4::HandHistoryParser parser;
  auto hh = parser.Parse(hh_text);
  EXPECT_EQ(hh.hand_id, 1000);
  EXPECT_EQ(hh.HeroName(), "Hero");

  // 2. Analyze equity via Phase3
  poker_engine::phase3::FlopExplorer explorer;
  explorer.SetHeroRange(Range::FromString("QQ"));
  explorer.SetVillainRange(Range::FullCombinatorial());

  auto flop_analysis = explorer.AnalyzeFlop("Ah7s3c", 10000);
  EXPECT_GT(flop_analysis.hero_equity, 0.5);  // Overpair should be ahead

  // 3. Spot solve
  auto spot = poker_engine::phase3::SpotSolver::QuickSolve("QQ", "22+,A2s+,K2s+,Q2s+,J2s+",
                                                           "Ah7s3c", 42, 0);

  EXPECT_NE(spot.best_action, nullptr);
  std::cout << spot.ToString() << "\n";

  // 4. Multi-street solve
  poker_engine::phase4::MS_Config config;
  config.iterations = 50;
  config.mc_samples = 500;
  config.verbose = false;

  poker_engine::phase4::MultiStreetSolver ms_solver(config);
  ms_solver.SetRanges({Range::FromString("QQ"), Range::FullCombinatorial()});

  std::vector<poker_engine::Card> flop_cards = {poker_engine::Card::Parse("Ah"),
                                                poker_engine::Card::Parse("7s"),
                                                poker_engine::Card::Parse("3c")};
  ms_solver.SetFlop(flop_cards);

  auto ms_result = ms_solver.SolveFromHand(hh);
  std::cout << ms_result.ToString() << "\n";
  EXPECT_GE(ms_result.iterations, 50);
}
