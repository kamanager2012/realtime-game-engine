#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "poker_engine/evaluator/card.h"
#include "poker_engine/evaluator/evaluator.h"

namespace poker_engine {
namespace phase1 {

enum class ActionType : uint8_t {
  FOLD = 0,
  CHECK,
  CALL,
  BET,
  RAISE,
  ALL_IN,
  POST_SB,
  POST_BB,
  COLLECT,
  SHOW_WIN,
  SHOW_TIE,
  UNCALLED_BET_RETURN
};

std::string ActionTypeToString(ActionType a);

enum class Street : uint8_t { PREFLOP = 0, FLOP, TURN, RIVER, SHOWDOWN };

// 单个行动
struct PlayerAction {
  std::string player_name;
  ActionType action;
  double amount;          // 涉及金额 (raise/bet/call/post)
  double total_invested;  // 该行动后本街累计投入
  Street street;
  std::string raw_line;  // 原始文本（调试用）

  std::string ToString() const;
};

// 一条街的完整行动序列
struct StreetRound {
  Street street;
  std::vector<Card> community_cards;  // 该街发出的公共牌
  std::vector<PlayerAction> actions;
  double pot_before;  // 该街开始前的底池
};

// 单个底池的结果
struct PotResult {
  double amount;
  std::vector<std::string> winners;  // 赢家列表
  HandCategory winning_category;
  std::vector<uint8_t> winning_cards;  // 赢家的5张决胜牌（rank index）
};

// 牌局汇总
struct HandHistory {
  int64_t hand_id = 0;
  std::string site;
  std::string game_type;  // "No Limit Hold'em" | "Pot Limit Omaha" 等
  std::string tournament_id;
  std::string table_name;
  double small_blind = 0;
  double big_blind = 0;
  int max_seats = 0;
  int button_seat = -1;
  std::string hero_name;  // 我们观测的玩家

  std::map<std::string, double> starting_stacks;  // 玩家 -> 初始筹码
  std::vector<std::string> seat_order;            // 按座位顺序

  Card hero_cards[2];  // hero 底牌
  bool hero_was_dealer = false;
  bool hero_cards_known_ = false;

  std::vector<StreetRound> streets;  // PREFLOP ~ RIVER
  std::vector<Card> board;           // 所有 5 张公共牌
  std::vector<PotResult> pots;
  double total_pot = 0;

  // 工具方法
  bool IsHero(const std::string& name) const { return name == hero_name; }
  std::optional<PlayerAction> HeroActionOnStreet(Street s) const;
  double HeroTotalInvested() const;
  double HeroNetProfit() const;
  std::string Summary() const;
};

}  // namespace phase1
}  // namespace poker_engine
