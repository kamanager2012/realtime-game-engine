#pragma once
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/phase1/hand_history.h"
#include "poker_engine/phase2/solver_node.h"

namespace poker_engine {
namespace phase3 {

// 单手牌的可视化回放
struct ReplayNode {
  std::string street_name;
  std::string action_desc;
  std::string player;
  double pot_before;
  double amount;

  // EV 分析结果
  double hero_equity = 0;
  double villain_equity = 0;
  std::optional<double> hero_ev;  // 如果能计算
  std::string recommendation;     // "+" (好) / "-" (差) / "=" (中性)
  double ev_diff = 0;             // 实际EV - 最优EV

  std::string ToString() const;
};

struct ReplayResult {
  int64_t hand_id;
  std::string hero_name;
  std::string hero_cards_str;
  std::string result_type;  // "Win $X" / "Lose $X" / "Tie"
  double net_profit;

  std::vector<ReplayNode> nodes;
  double total_hero_ev = 0;
  double total_actual = 0;
  int mistakes = 0;
  int good_plays = 0;

  std::string Summary() const;
  std::string FullReport() const;
};

class HandReplayer {
 public:
  HandReplayer();

  // 设置 Hero 手牌 (用于精确分析)
  void SetHeroCards(const std::string& card1, const std::string& card2);

  // 设置对手范围
  void SetVillainRange(const std::string& range_str);

  // 回放一手牌
  ReplayResult Replay(const poker_engine::phase1::HandHistory& hh);

  // 快速回放 (输入字符串列表)
  static ReplayResult QuickReplay(const std::string& hand_text);

 private:
  std::string hero_card1_, hero_card2_;
  std::string villain_range_str_;
  std::mt19937 rng_{42};
};

}  // namespace phase3
}  // namespace poker_engine
