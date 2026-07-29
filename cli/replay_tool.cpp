#include <fstream>
#include <iostream>
#include <string>

#include "poker_engine/phase1/ev_replay.h"
#include "poker_engine/phase1/hand_history.h"

using namespace poker_engine::phase1;
using poker_engine::Card;
using poker_engine::Rank;
using poker_engine::Suit;

// Stub parser — 读取文件并构建 HandHistory
// 当前版本：读取预定义测试数据
// 完整解析器将在后续迭代实现

HandHistory LoadFromRawFile(const std::string& filepath) {
  HandHistory hh;
  hh.hand_id = 1;
  hh.site = "PokerStars";
  hh.game_type = "No Limit Hold'em";
  hh.table_name = "Table 1";
  hh.small_blind = 5.0;
  hh.big_blind = 10.0;
  hh.max_seats = 6;
  hh.hero_name = "Hero";
  hh.starting_stacks["Hero"] = 1000.0;
  hh.starting_stacks["Villain"] = 1000.0;

  // 示例牌局数据
  StreetRound preflop;
  preflop.street = Street::PREFLOP;
  preflop.pot_before = 15.0;  // SB + BB

  PlayerAction sb;
  sb.player_name = "Hero";
  sb.action = ActionType::POST_SB;
  sb.amount = 5.0;
  sb.street = Street::PREFLOP;
  PlayerAction bb;
  bb.player_name = "Villain";
  bb.action = ActionType::POST_BB;
  bb.amount = 10.0;
  bb.street = Street::PREFLOP;
  PlayerAction raise1;
  raise1.player_name = "Hero";
  raise1.action = ActionType::RAISE;
  raise1.amount = 30.0;
  raise1.street = Street::PREFLOP;
  PlayerAction call1;
  call1.player_name = "Villain";
  call1.action = ActionType::CALL;
  call1.amount = 25.0;
  call1.street = Street::PREFLOP;

  preflop.actions = {sb, bb, raise1, call1};
  hh.streets.push_back(preflop);

  // Hero 底牌
  hh.hero_cards[0] = Card(Rank::Ace, Suit::Spades);
  hh.hero_cards[1] = Card(Rank::King, Suit::Hearts);
  hh.hero_cards_known_ = true;

  // Flop
  StreetRound flop;
  flop.street = Street::FLOP;
  flop.pot_before = 75.0;
  flop.community_cards.push_back(Card(Rank::Queen, Suit::Diamonds));
  flop.community_cards.push_back(Card(Rank::Jack, Suit::Clubs));
  flop.community_cards.push_back(Card(Rank::Seven, Suit::Hearts));
  PlayerAction cb;
  cb.player_name = "Hero";
  cb.action = ActionType::BET;
  cb.amount = 40.0;
  cb.street = Street::FLOP;
  PlayerAction fold1;
  fold1.player_name = "Villain";
  fold1.action = ActionType::FOLD;
  fold1.street = Street::FLOP;
  flop.actions = {cb, fold1};
  hh.streets.push_back(flop);

  // 公共牌
  hh.board = flop.community_cards;

  // 底池
  PotResult pot;
  pot.amount = 145.0;
  pot.winners = {"Hero"};
  hh.pots.push_back(pot);
  hh.total_pot = 145.0;

  return hh;
}

int main(int argc, char* argv[]) {
  std::cout << "=== Poker Replay Tool v0.1 ===\n\n";

  // 如果有参数，尝试读取文件
  if (argc >= 2) {
    std::string filepath = argv[1];
    std::cout << "Loading: " << filepath << "\n";
    // TODO: 完整解析器
  }

  // 使用示例牌局
  HandHistory hh = LoadFromRawFile("");
  std::cout << hh.Summary() << "\n\n";

  // EV 回溯
  EVReplayer replayer;
  if (hh.hero_cards_known_) {
    replayer.SetHeroCards(hh.hero_cards);
  }

  auto result = replayer.Replay(hh, 10000);
  std::cout << result.DetailedReport() << "\n";

  return 0;
}
