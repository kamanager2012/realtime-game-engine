#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/phase1/hand_history.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase1 {

// 单个决策点的 EV 分析
struct NodeEV {
  std::string player;
  Street street;
  std::string action_desc;  // "folds" / "calls $50" / "raises to $150"
  int action_index;         // 在该街行动序列中的位置

  // 基于对手范围模拟的 equity
  double equity = 0;  // 0~1
  double hand_wins = 0;
  double hand_ties = 0;
  int64_t trials = 0;

  // 即时 EV
  double pot_size = 0;      // 行动前底池
  double call_cost = 0;     // 需要投入的金额
  double immediate_ev = 0;  // equity * (pot + call_cost) - (1-equity) * call_cost

  // 终局结果（摊牌后回溯填）
  std::optional<double> actual_outcome;  // 实际赢得/输掉的金额

  std::string ToString() const;
};

// 整手牌的 EV 回溯结果
struct EVReplayResult {
  int64_t hand_id = 0;
  std::string hero;
  std::vector<NodeEV> nodes;  // 按时间顺序的每个决策点
  double hero_starting_stack = 0;
  double hero_ending_stack = 0;
  double total_invested = 0;
  double total_won = 0;
  double overall_ev = 0;  // 所有决策点 EV 的折现和

  // 聚合统计
  int n_decisions = 0;
  int n_correct_folds = 0;  // EV >= 0 的弃牌（持有好 equity）
  int n_mistakes = 0;       // EV < 0 的跟注/加注
  double total_ev_saved = 0;

  std::string DetailedReport() const;
  std::string CompactReport() const;
};

// EV 回溯引擎
class EVReplayer {
 public:
  // 构造器
  EVReplayer() = default;

  // 设置 hero 的范围估计（对手视角）
  void SetOpponentRange(const std::string& player, const poker_engine::range::Range& r);

  // 设置 hero 的已知手牌
  void SetHeroCards(const Card cards[2]);

  // 设置已知公共牌
  void SetBoard(const std::vector<Card>& board, int street_idx);

  // 从 HandHistory 执行完整回溯
  EVReplayResult Replay(const HandHistory& hh, int n_samples = 20000);

 private:
  std::map<std::string, poker_engine::range::Range> opp_ranges_;
  Card hero_cards_[2] = {Card(0), Card(0)};
  bool hero_cards_known_ = false;
  std::mt19937 rng_{42};

  // 内部方法
  NodeEV EvaluateNode(const HandHistory& hh, int street_idx, int action_idx,
                      const poker_engine::range::Range& my_range, double pot_before,
                      double to_call);
};

}  // namespace phase1
}  // namespace poker_engine
