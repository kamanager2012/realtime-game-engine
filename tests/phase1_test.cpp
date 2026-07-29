#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/phase1/ev_replay.h"
#include "poker_engine/phase1/hand_history.h"
#include "poker_engine/range/range.h"

using namespace poker_engine::phase1;
using poker_engine::Card;
using poker_engine::Rank;
using poker_engine::Suit;
using poker_engine::range::Range;

// =========== 数据结构测试 ===========

TEST(Phase1Test, HandHistoryEmpty) {
  HandHistory hh;
  EXPECT_EQ(hh.hand_id, 0);
  EXPECT_TRUE(hh.streets.empty());
  EXPECT_TRUE(hh.board.empty());
  EXPECT_EQ(hh.total_pot, 0);
}

TEST(Phase1Test, ActionTypeToString) {
  PlayerAction a;
  a.player_name = "TestPlayer";
  a.action = ActionType::FOLD;
  EXPECT_NE(a.ToString().find("folds"), std::string::npos);

  a.action = ActionType::RAISE;
  a.amount = 100;
  EXPECT_NE(a.ToString().find("raises"), std::string::npos);
}

TEST(Phase1Test, HeroActionOnStreet) {
  HandHistory hh;
  StreetRound sr;
  sr.street = Street::PREFLOP;

  PlayerAction a1;
  a1.player_name = "Villain";
  a1.action = ActionType::RAISE;
  a1.amount = 30;
  sr.actions.push_back(a1);

  PlayerAction a2;
  a2.player_name = "Hero";
  a2.action = ActionType::CALL;
  a2.amount = 25;
  sr.actions.push_back(a2);

  hh.streets.push_back(sr);
  hh.hero_name = "Hero";

  auto hero_action = hh.HeroActionOnStreet(Street::PREFLOP);
  ASSERT_TRUE(hero_action.has_value());
  EXPECT_EQ(hero_action->action, ActionType::CALL);
  EXPECT_EQ(hero_action->amount, 25);
}

TEST(Phase1Test, HeroNetProfit) {
  HandHistory hh;
  hh.hero_name = "Hero";
  hh.starting_stacks["Hero"] = 1000;

  // Hero 投资 200
  StreetRound sr;
  sr.street = Street::PREFLOP;
  PlayerAction a;
  a.player_name = "Hero";
  a.action = ActionType::RAISE;
  a.amount = 200;
  a.street = Street::PREFLOP;
  sr.actions.push_back(a);
  hh.streets.push_back(sr);

  // Hero 赢得底池
  PotResult pot;
  pot.amount = 400;
  pot.winners = {"Hero"};
  hh.pots.push_back(pot);
  hh.total_pot = 400;

  EXPECT_DOUBLE_EQ(hh.HeroTotalInvested(), 200);
  EXPECT_DOUBLE_EQ(hh.HeroNetProfit(), 200);  // 400 - 200
}

TEST(Phase1Test, SummaryContainsKeyInfo) {
  HandHistory hh;
  hh.hand_id = 12345;
  hh.site = "PokerStars";
  hh.game_type = "No Limit Hold'em";
  hh.hero_name = "TestPlayer";
  hh.board = {Card(Rank::Ace, Suit::Spades), Card(Rank::King, Suit::Hearts)};
  hh.total_pot = 150.0;

  // Hero 底牌已知
  hh.hero_cards[0] = Card(Rank::Ten, Suit::Diamonds);
  hh.hero_cards[1] = Card(Rank::Jack, Suit::Clubs);
  hh.hero_cards_known_ = true;

  std::string s = hh.Summary();
  EXPECT_NE(s.find("12345"), std::string::npos);
  EXPECT_NE(s.find("PokerStars"), std::string::npos);
  EXPECT_NE(s.find("TestPlayer"), std::string::npos);
  EXPECT_NE(s.find("Td"), std::string::npos);
  EXPECT_NE(s.find("Jc"), std::string::npos);
}

// =========== EV 回溯测试 ===========

TEST(Phase1Test, EVReplayerOnPocketAces) {
  HandHistory hh;
  hh.hand_id = 1;
  hh.hero_name = "Hero";
  hh.game_type = "No Limit Hold'em";
  hh.starting_stacks["Hero"] = 1000;
  hh.starting_stacks["Villain"] = 1000;

  // Hero 持有 AA
  hh.hero_cards[0] = Card(Rank::Ace, Suit::Spades);
  hh.hero_cards[1] = Card(Rank::Ace, Suit::Hearts);
  hh.hero_cards_known_ = true;

  // PREFLOP 行动
  StreetRound preflop;
  preflop.street = Street::PREFLOP;
  preflop.pot_before = 15;  // SB+BB
  PlayerAction a1;
  a1.player_name = "Villain";
  a1.action = ActionType::RAISE;
  a1.amount = 30;
  a1.street = Street::PREFLOP;
  PlayerAction a2;
  a2.player_name = "Hero";
  a2.action = ActionType::RAISE;
  a2.amount = 90;
  a2.street = Street::PREFLOP;
  preflop.actions = {a1, a2};
  hh.streets.push_back(preflop);

  EVReplayer replayer;
  replayer.SetHeroCards(hh.hero_cards);

  // 设置对手范围为典型翻前
  replayer.SetOpponentRange("Villain",
                            Range::FromString("22+,A2s+,K2s+,Q2s+,J2s+,T2s+,A2o+,K2o+,Q2o+"));

  auto result = replayer.Replay(hh, 10000);

  // Hero 持有 AA 是巨大的优势
  std::cout << result.CompactReport() << "\n";
  std::cout << result.DetailedReport() << "\n";

  // AA 应该有很大的 Equity 优势
  ASSERT_GT(result.nodes.size(), 0);
  for (const auto& node : result.nodes) {
    if (node.player == "Hero") {
      EXPECT_GT(node.equity, 0.40);
    }
  }
}

TEST(Phase1Test, EVReplayerMultipleDecisions) {
  HandHistory hh;
  hh.hand_id = 2;
  hh.hero_name = "Hero";
  hh.game_type = "No Limit Hold'em";
  hh.starting_stacks["Hero"] = 1000;
  hh.starting_stacks["Villain"] = 1000;

  // Hero 持有 AKs - 强但不是 AA 级别
  hh.hero_cards[0] = Card(Rank::Ace, Suit::Spades);
  hh.hero_cards[1] = Card(Rank::King, Suit::Spades);
  hh.hero_cards_known_ = true;

  // PREFLOP
  StreetRound preflop;
  preflop.street = Street::PREFLOP;
  preflop.pot_before = 15;
  PlayerAction a1;
  a1.player_name = "Villain";
  a1.action = ActionType::RAISE;
  a1.amount = 30;
  a1.street = Street::PREFLOP;
  PlayerAction a2;
  a2.player_name = "Hero";
  a2.action = ActionType::CALL;
  a2.amount = 25;
  a2.street = Street::PREFLOP;
  preflop.actions = {a1, a2};
  hh.streets.push_back(preflop);

  // FLOP
  StreetRound flop;
  flop.street = Street::FLOP;
  flop.pot_before = 70;
  Card qs = Card(Rank::Queen, Suit::Spades);
  Card js = Card(Rank::Jack, Suit::Spades);
  Card th = Card(Rank::Trey, Suit::Hearts);
  flop.community_cards.push_back(qs);
  flop.community_cards.push_back(js);
  flop.community_cards.push_back(th);
  PlayerAction a3;
  a3.player_name = "Villain";
  a3.action = ActionType::BET;
  a3.amount = 50;
  a3.street = Street::FLOP;
  PlayerAction a4;
  a4.player_name = "Hero";
  a4.action = ActionType::CALL;
  a4.amount = 50;
  a4.street = Street::FLOP;
  flop.actions = {a3, a4};
  hh.streets.push_back(flop);

  EVReplayer replayer;
  replayer.SetHeroCards(hh.hero_cards);
  replayer.SetOpponentRange("Villain", Range::FullCombinatorial());

  auto result = replayer.Replay(hh, 10000);
  std::cout << result.CompactReport() << "\n";

  ASSERT_GT(result.nodes.size(), 1);
}

TEST(Phase1Test, EVReplayerEmptyHistory) {
  HandHistory hh;
  EVReplayer replayer;
  auto result = replayer.Replay(hh, 1000);
  EXPECT_EQ(result.nodes.size(), 0);
  EXPECT_EQ(result.n_decisions, 0);
  EXPECT_DOUBLE_EQ(result.overall_ev, 0);
}

// =========== EVReplayResult 格式化输出测试 ===========

TEST(Phase1Test, EVReplayResultReports) {
  EVReplayResult result;
  result.hand_id = 123;
  result.hero = "TestPlayer";
  result.total_invested = 100;
  result.total_won = 250;
  result.overall_ev = 150;
  result.hero_starting_stack = 1000;
  result.hero_ending_stack = 1150;
  result.n_decisions = 5;
  result.n_mistakes = 1;

  std::string detail = result.DetailedReport();
  EXPECT_NE(detail.find("123"), std::string::npos);
  EXPECT_NE(detail.find("TestPlayer"), std::string::npos);

  std::string compact = result.CompactReport();
  EXPECT_NE(compact.find("123"), std::string::npos);
  EXPECT_NE(compact.find("EV=$"), std::string::npos);
}

TEST(Phase1Test, NodeEVToString) {
  NodeEV node;
  node.player = "Hero";
  node.action_desc = "calls $50";
  node.equity = 0.65;
  node.immediate_ev = 15.5;
  node.trials = 10000;

  std::string s = node.ToString();
  EXPECT_NE(s.find("Hero"), std::string::npos);
  EXPECT_NE(s.find("65%"), std::string::npos);
  EXPECT_NE(s.find("15.5"), std::string::npos);
}

// =========== 边界情况 ===========

TEST(Phase1Test, HandHistoryWithMultiplePots) {
  HandHistory hh;
  hh.hero_name = "Hero";
  hh.starting_stacks["Hero"] = 1000;
  hh.starting_stacks["SidePlayer"] = 500;

  PotResult main_pot;
  main_pot.amount = 300;
  main_pot.winners = {"Hero"};

  PotResult side_pot;
  side_pot.amount = 100;
  side_pot.winners = {"SidePlayer"};

  hh.pots = {main_pot, side_pot};
  hh.total_pot = 400;

  double profit = hh.HeroNetProfit();
  EXPECT_DOUBLE_EQ(profit, 300 - hh.HeroTotalInvested());
}
