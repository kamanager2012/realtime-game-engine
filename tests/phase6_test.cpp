#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/phase4/hh_parser.h"
#include "poker_engine/phase6/api_server.h"
#include "poker_engine/phase6/hand_database.h"
#include "poker_engine/phase6/icfr_solver.h"
#include "poker_engine/phase6/range_tracker.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase6;
using namespace poker_engine::range;

// ============ ICFR SOLVER TESTS ============

TEST(Phase6Test, ICFRBasicCreation) {
  ICFRSolver solver(ICFRConfig{100, 500, 1.0, 0.5, false});
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+"));

  EXPECT_TRUE(solver.GetStrategy({}).size() > 0);
}

TEST(Phase6Test, ICFRNodeDefault) {
  ICNode node;
  EXPECT_EQ(node.visit_count, 0);

  double strat[NUM_IC_ACTIONS];
  node.GetStrategy(strat);
  double sum = 0;
  for (int a = 0; a < NUM_IC_ACTIONS; a++) {
    EXPECT_GE(strat[a], 0);
    sum += strat[a];
  }
  EXPECT_NEAR(sum, 1.0, 0.001);
}

TEST(Phase6Test, ICFRRegretMatching) {
  ICNode node;
  node.cumulative_regret[static_cast<int>(ICAction::FOLD)] = -100;
  node.cumulative_regret[static_cast<int>(ICAction::CALL)] = 200;
  node.cumulative_regret[static_cast<int>(ICAction::BET_POT)] = 50;
  node.cumulative_regret[static_cast<int>(ICAction::CHECK)] = -50;
  node.cumulative_regret[static_cast<int>(ICAction::ALL_IN)] = 0;
  node.cumulative_regret[static_cast<int>(ICAction::BET_50)] = -10;

  double strat[NUM_IC_ACTIONS];
  node.GetStrategy(strat);

  EXPECT_NEAR(strat[static_cast<int>(ICAction::FOLD)], 0.0, 0.001);
  EXPECT_NEAR(strat[static_cast<int>(ICAction::CHECK)], 0.0, 0.001);
  EXPECT_NEAR(strat[static_cast<int>(ICAction::BET_50)], 0.0, 0.001);
  EXPECT_GT(strat[static_cast<int>(ICAction::CALL)], 0.5);
  EXPECT_GT(strat[static_cast<int>(ICAction::BET_POT)], 0.1);

  double sum = 0;
  for (int a = 0; a < NUM_IC_ACTIONS; a++) sum += strat[a];
  EXPECT_NEAR(sum, 1.0, 0.001);
}

TEST(Phase6Test, ICFRMiniSolve) {
  ICFRConfig config;
  config.iterations = 50;
  config.mc_samples = 500;
  config.verbose = false;

  ICFRSolver solver(config);
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+,A2s+"));

  auto result = solver.Solve();
  EXPECT_GE(result.iterations, 50);
  EXPECT_GE(result.strategy_profile.size(), 0);
  EXPECT_GE(result.exploitability_vs_blueprint, 0);
}

TEST(Phase6Test, ICFRMultiStreet) {
  ICFRConfig config;
  config.iterations = 50;
  config.mc_samples = 500;
  config.verbose = false;

  ICFRSolver solver(config);
  solver.SetHeroRange(Range::FromString("AA,KK,QQ"));
  solver.SetVillainRange(Range::FromString("22+,A2s+,K2s+"));

  std::vector<Card> flop = {Card::Parse("Qh"), Card::Parse("Jd"), Card::Parse("7c")};
  solver.SetBoard(flop);

  auto result = solver.Solve();
  EXPECT_GE(result.strategy_profile.size(), 0);

  for (const auto& [key, strat] : result.strategy_profile) {
    double sum = 0;
    for (double s : strat) sum += s;
    EXPECT_NEAR(sum, 1.0, 0.01) << "Strategy sum != 1 for " << key.Key();
  }
}

TEST(Phase6Test, ICFRExploitability) {
  ICFRConfig config;
  config.iterations = 100;
  config.mc_samples = 1000;
  config.verbose = false;

  ICFRSolver solver(config);
  solver.SetHeroRange(Range::FromString("AA"));
  solver.SetVillainRange(Range::FromString("77+,A2s+"));

  auto result = solver.Solve();
  EXPECT_GE(result.exploitability_vs_blueprint, 0);
  EXPECT_LT(result.exploitability_vs_blueprint, 10000);
}

TEST(Phase6Test, ICFRResultFormat) {
  ICFRResult result;
  result.iterations = 1000;
  result.exploitability_vs_blueprint = 15.5;
  result.achieved_ev = 2.3;

  ICInfoSetKey key{1, "ck", 100, 25};
  result.strategy_profile[key] = {0.1, 0.5, 0.3, 0.0, 0.1, 0.0};

  std::string s = result.ToString();
  EXPECT_NE(s.find("Imitation CFR"), std::string::npos);
  EXPECT_NE(s.find("1000"), std::string::npos);
  EXPECT_NE(s.find("CHECK"), std::string::npos);
}

// ============ RANGE TRACKER TESTS ============

TEST(Phase6Test, TrackerInitialization) {
  RangeTracker tracker;
  tracker.SetPriorRange("22+,A2s+");

  auto result = tracker.GetCurrentRange();
  EXPECT_GT(result.remaining_combos, 0);
  EXPECT_LE(result.remaining_combos, 1326);
  EXPECT_NEAR(result.confidence, 0.0, 0.01);
  EXPECT_NE(result.reasoning.find("narrowed"), std::string::npos);
}

TEST(Phase6Test, TrackerAfterRaise) {
  RangeTracker tracker;
  tracker.SetPriorRange("77+,A2s+,K2s+,Q2s+");

  TrackingObservation obs;
  obs.street = 0;
  obs.action = ActionObserved::RAISE;
  obs.amount = 7;
  obs.pot = 3;
  tracker.ObserveAction(obs);

  auto result = tracker.GetCurrentRange();
  EXPECT_LT(result.remaining_combos, 400);
  EXPECT_GT(result.confidence, 0.0);
}

TEST(Phase6Test, TrackerAfterFolds) {
  RangeTracker tracker;
  tracker.SetPriorRange("22+,A2s+,K2s+,Q2s+");

  TrackingObservation obs;
  obs.street = 0;
  obs.action = ActionObserved::FOLD;
  obs.amount = 0;
  obs.pot = 3;
  tracker.ObserveAction(obs);

  auto result = tracker.GetCurrentRange();
  EXPECT_GT(result.remaining_combos, 0);
}

TEST(Phase6Test, TrackerMultipleActions) {
  RangeTracker tracker;
  tracker.SetPriorRange("22+,A2s+,K2s+,Q2s+,J2s+,T2s+");

  TrackingObservation preflop;
  preflop.street = 0;
  preflop.action = ActionObserved::RAISE;
  preflop.amount = 12;
  preflop.pot = 5;
  tracker.ObserveAction(preflop);

  TrackingObservation flop_bet;
  flop_bet.street = 1;
  flop_bet.action = ActionObserved::BET_MEDIUM;
  flop_bet.amount = 15;
  flop_bet.pot = 30;
  std::vector<Card> flop = {Card::Parse("Qh"), Card::Parse("Jd"), Card::Parse("7c")};
  flop_bet.community_cards = flop;
  tracker.ObserveAction(flop_bet);

  auto result = tracker.GetCurrentRange();
  EXPECT_LT(result.remaining_combos, 300);
  EXPECT_GT(result.confidence, 0.01);
}

TEST(Phase6Test, TrackerAggressionProfile) {
  RangeTracker tight_tracker;
  tight_tracker.SetPriorRange("22+,A2s+,K2s+");
  tight_tracker.SetAggressionProfile(0.9);

  tight_tracker.ObserveAction({0, ActionObserved::BET_LARGE, 50, 50, {}});
  auto tight_result = tight_tracker.GetCurrentRange();

  RangeTracker loose_tracker;
  loose_tracker.SetPriorRange("22+,A2s+,K2s+");
  loose_tracker.SetAggressionProfile(0.1);

  loose_tracker.ObserveAction({0, ActionObserved::BET_LARGE, 50, 50, {}});
  auto loose_result = loose_tracker.GetCurrentRange();

  EXPECT_LE(tight_result.remaining_combos, loose_result.remaining_combos);
}

TEST(Phase6Test, TrackerFullRange) {
  RangeTracker tracker;
  Range full = Range::FullCombinatorial();
  tracker.SetPriorRange(full);

  EXPECT_EQ(tracker.GetCurrentRange().remaining_combos, 1326);
}

TEST(Phase6Test, TrackerTopHands) {
  RangeTracker tracker;
  tracker.SetPriorRange("AA,KK,QQ,AKs");

  TrackingObservation obs;
  obs.street = 1;
  obs.action = ActionObserved::RAISE;
  obs.amount = 30;
  obs.pot = 100;
  tracker.ObserveAction(obs);

  auto result = tracker.GetCurrentRange();
  EXPECT_GT(result.top_hands.size(), 0);
  EXPECT_LE(result.top_hands.size(), 10);

  for (const auto& [hand, weight] : result.top_hands) {
    EXPECT_GT(weight, 0);
  }
}

// ============ HAND DATABASE TESTS ============

TEST(Phase6Test, DatabaseCreationInMemory) {
  auto db = std::make_shared<HandDatabase>();
  if (!db->Open(":memory:")) {
    GTEST_SKIP() << "SQLite3 not available";
  }

  EXPECT_TRUE(db->IsOpen());
  EXPECT_TRUE(db->CreateSchema());
  EXPECT_EQ(db->TotalHands(), 0);
  db->Close();
  EXPECT_FALSE(db->IsOpen());
}

TEST(Phase6Test, DatabaseCRUD) {
  auto db = std::make_shared<HandDatabase>();
  if (!db->Open(":memory:")) {
    GTEST_SKIP() << "SQLite3 not available";
  }
  db->CreateSchema();

  poker_engine::phase4::HandHistory hh;
  hh.hand_id = 1001;
  hh.site = "PokerStars";
  hh.game_type = "No Limit Hold'em";
  hh.table_name = "TestTable";
  hh.small_blind = 0.5;
  hh.big_blind = 1.0;
  hh.total_pot = 42;

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

  hh.hero_cards_[0] = Card::Parse("As");
  hh.hero_cards_[1] = Card::Parse("Ks");
  hh.hero_cards_known_ = true;

  poker_engine::phase4::HHResult res;
  res.player_name = "Hero";
  res.amount = 42;
  res.hand_desc = "pair of Kings";
  hh.results.push_back(res);

  hh.all_board_cards.push_back(Card(poker_engine::Rank::Queen, poker_engine::Suit::Diamonds));
  hh.all_board_cards.push_back(Card(poker_engine::Rank::Jack, poker_engine::Suit::Hearts));
  hh.all_board_cards.push_back(Card(poker_engine::Rank::Seven, poker_engine::Suit::Clubs));

  EXPECT_TRUE(db->InsertHand(hh));
  EXPECT_EQ(db->TotalHands(), 1);

  DBQueryOptions opts;
  opts.player_filter = "Hero";
  opts.limit = 10;

  auto results = db->QueryHands(opts);
  ASSERT_EQ(results.size(), 1);
  EXPECT_EQ(results[0].hand_id, 1001);
  EXPECT_EQ(results[0].hero_name, "Hero");
}

TEST(Phase6Test, DatabaseMultipleHands) {
  auto db = std::make_shared<HandDatabase>();
  if (!db->Open(":memory:")) {
    GTEST_SKIP() << "SQLite3 not available";
  }
  db->CreateSchema();

  std::vector<poker_engine::phase4::HandHistory> hands;
  for (int i = 0; i < 50; i++) {
    poker_engine::phase4::HandHistory hh;
    hh.hand_id = 2000 + i;
    hh.site = (i % 2 == 0) ? "PokerStars" : "GGNetwork";
    hh.game_type = "No Limit Hold'em";
    hh.table_name = "Table" + std::to_string(i);
    hh.small_blind = 0.5;
    hh.big_blind = 1.0;
    hh.total_pot = 20 + (i * 5);

    poker_engine::phase4::HHSeat s;
    s.seat_no = 1;
    s.player_name = "Player" + std::to_string(i % 3);
    s.stack = 100 + i * 10;
    s.is_hero = (i % 3 == 0);
    hh.seats.push_back(s);
    hh.seat_map[s.player_name] = s;

    hh.hero_cards_[0] = Card::Parse("As");
    hh.hero_cards_[1] = Card::Parse("Kd");
    hh.hero_cards_known_ = true;

    poker_engine::phase4::HHResult res;
    res.player_name = s.player_name;
    res.amount = hh.total_pot * 0.5;
    hh.results.push_back(res);

    hands.push_back(hh);
  }

  db->InsertHands(hands);
  EXPECT_EQ(db->TotalHands(), 50);

  DBQueryOptions opts;
  opts.site_filter = "PokerStars";
  auto results = db->QueryHands(opts);
  EXPECT_EQ(results.size(), 25);

  opts = DBQueryOptions();
  auto agg = db->GetAggregates(opts);
  EXPECT_EQ(agg.hand_count, 50);

  auto players = db->GetPlayers();
  EXPECT_GE(players.size(), 2);

  auto sites = db->GetSites();
  EXPECT_EQ(sites.size(), 2);
}

TEST(Phase6Test, DatabaseDelete) {
  auto db = std::make_shared<HandDatabase>();
  if (!db->Open(":memory:")) {
    GTEST_SKIP() << "SQLite3 not available";
  }
  db->CreateSchema();

  poker_engine::phase4::HandHistory hh;
  hh.hand_id = 3001;
  hh.site = "Test";
  hh.game_type = "NLHE";
  hh.total_pot = 100;
  poker_engine::phase4::HHSeat s;
  s.seat_no = 1;
  s.player_name = "Hero";
  s.stack = 100;
  s.is_hero = true;
  hh.seats.push_back(s);
  hh.seat_map["Hero"] = s;

  db->InsertHand(hh);
  EXPECT_EQ(db->TotalHands(), 1);

  db->DeleteHand(3001);
  EXPECT_EQ(db->TotalHands(), 0);
}

TEST(Phase6Test, DatabaseDateRange) {
  auto db = std::make_shared<HandDatabase>();
  if (!db->Open(":memory:")) {
    GTEST_SKIP() << "SQLite3 not available";
  }
  db->CreateSchema();

  for (int i = 1; i <= 10; i++) {
    poker_engine::phase4::HandHistory hh;
    hh.hand_id = 4000 + i;
    hh.site = "Stars";
    hh.game_type = "NLHE";
    hh.total_pot = 20 + i;
    poker_engine::phase4::HHSeat s;
    s.seat_no = 1;
    s.player_name = "TestPlayer";
    s.stack = 100;
    s.is_hero = true;
    hh.seats.push_back(s);
    hh.seat_map["TestPlayer"] = s;
    db->InsertHand(hh);
  }

  DBQueryOptions opts;
  opts.date_from = "2020-01-01";
  opts.date_to = "2030-12-31";
  auto results = db->QueryHands(opts);
  EXPECT_EQ(results.size(), 10);
}

// ============ API SERVER TESTS (without HTTP) ============

TEST(Phase6Test, APIResponseFormat) {
  std::string resp = APIResponse::JSONResponse(200, "OK", "{\"data\":123}");
  EXPECT_NE(resp.find("\"status\":200"), std::string::npos);
  EXPECT_NE(resp.find("\"OK\""), std::string::npos);

  std::string err = APIResponse::JSONResponse(404, "Not Found");
  EXPECT_NE(err.find("\"status\":404"), std::string::npos);
}

TEST(Phase6Test, APIServerCreation) {
  APIServer server(APIConfig{"0.0.0.0", 9999, 1});
  EXPECT_EQ(server.GetPort(), 9999);
  EXPECT_FALSE(server.IsRunning());
}

TEST(Phase6Test, APIQueryParsing) {
  std::string query = "hero=AKs&villain=22+&flop=QhJd7c";
  auto params = APIResponse::ParseQuery(query);

  EXPECT_EQ(params["hero"], "AKs");
  EXPECT_EQ(params["villain"], "22+");
  EXPECT_EQ(params["flop"], "QhJd7c");
}

TEST(Phase6Test, APIQueryEmpty) {
  auto params = APIResponse::ParseQuery("");
  EXPECT_TRUE(params.empty());
}

// ============ INTEGRATION ============

TEST(Phase6Test, ICFRvsBlueprintStock) {
  BlueprintStrategy blueprint;
  blueprint["0:ck:pot1500_to_call0"] = {{0.3, 0.7, 0.0, 0.0, 0.0, 0.0}};
  blueprint["0:ckck:pot3000_to_call0"] = {{0.4, 0.6, 0.0, 0.0, 0.0, 0.0}};
  blueprint["0:ckb:pot3000_to_call1500"] = {{0.1, 0.2, 0.5, 0.1, 0.1, 0.0}};

  ICFRConfig config;
  config.iterations = 200;
  config.mc_samples = 1000;
  config.verbose = false;

  ICFRSolver solver(config);
  solver.SetHeroRange(Range::FromString("AKs,AQs,AJs"));
  solver.SetVillainRange(Range::FromString("22+,A2s+"));
  solver.SetOpponentBlueprint(blueprint);

  auto result = solver.Solve();
  EXPECT_GE(result.strategy_profile.size(), 0);
  EXPECT_GE(result.iterations, 200);
}

TEST(Phase6Test, EdgeCaseICFREmptyBlueprint) {
  ICFRSolver solver(ICFRConfig{50, 500, 1.0, 0.5, false});
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+"));
  auto result = solver.Solve();
  EXPECT_GE(result.iterations, 50);
}

TEST(Phase6Test, EdgeCaseTrackerEmptyRange) {
  RangeTracker tracker;
  tracker.SetPriorRange("");
  auto result = tracker.GetCurrentRange();
  EXPECT_EQ(result.remaining_combos, 0);
}

TEST(Phase6Test, EdgeCaseTrackerSingleHandPrior) {
  RangeTracker tracker;
  tracker.SetPriorRange("AA");
  tracker.ObserveAction({0, ActionObserved::RAISE, 7, 3, {}});
  auto result = tracker.GetCurrentRange();
  EXPECT_GE(result.remaining_combos, 0);
  EXPECT_LE(result.remaining_combos, 6);
}

TEST(Phase6Test, EdgeCaseDatabaseNone) {
  auto db = std::make_shared<HandDatabase>();
  if (!db->Open(":memory:")) {
    GTEST_SKIP() << "SQLite3 not available";
  }
  db->CreateSchema();

  DBQueryOptions opts;
  opts.player_filter = "NonExistentPlayer";
  auto results = db->QueryHands(opts);
  EXPECT_TRUE(results.empty());

  auto agg = db->GetAggregates(opts);
  EXPECT_EQ(agg.hand_count, 0);
  EXPECT_EQ(agg.total_net, 0);
}

TEST(Phase6Test, EdgeCaseICFRHighIterations) {
  ICFRConfig config;
  config.iterations = 50;
  config.mc_samples = 100;
  config.verbose = false;

  ICFRSolver solver(config);
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(Range::FromString("22+,A2s+"));

  auto result = solver.Solve();
  EXPECT_GE(result.strategy_profile.size(), 0);
  EXPECT_EQ(result.iterations, 50);
}

TEST(Phase6Test, ICRBActionNames) {
  EXPECT_STREQ(ICActionName[0], "FOLD");
  EXPECT_STREQ(ICActionName[1], "CHECK");
  EXPECT_STREQ(ICActionName[2], "CALL");
  EXPECT_STREQ(ICActionName[3], "BET_50%");
  EXPECT_STREQ(ICActionName[4], "BET_POT");
  EXPECT_STREQ(ICActionName[5], "ALL_IN");
}

TEST(Phase6Test, InteractionTrackerVsICFR) {
  RangeTracker tracker;
  tracker.SetPriorRange("22+,A2s+,K2s+,Q2s+,J2s+");
  tracker.ObserveAction({1, ActionObserved::RAISE, 12, 5, {}});
  tracker.ObserveAction(
      {2, ActionObserved::CALL, 25, 30, {Card::Parse("Qh"), Card::Parse("Jd"), Card::Parse("7c")}});

  auto result = tracker.GetCurrentRange();

  ICFRSolver solver(ICFRConfig{10, 50, 1.0, 0.5, false});
  solver.SetHeroRange(Range::FromString("AKs"));
  solver.SetVillainRange(result.narrowed_range);
  solver.SetBoard({Card::Parse("Qh"), Card::Parse("Jd"), Card::Parse("7c")});

  auto icfr_result = solver.Solve();
  EXPECT_GE(icfr_result.iterations, 10);
}
