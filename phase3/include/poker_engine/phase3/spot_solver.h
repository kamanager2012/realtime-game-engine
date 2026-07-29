#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/phase2/range_builder.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase3 {

// 单点求解: 给定范围 + 牌面, 找到最优策略
struct SpotAction {
  std::string desc;          // "Fold", "Check", "Call", "Bet X", "All-in"
  double ev;                 // 期望价值 ($)
  double equity;             // 胜率
  double freq;               // 混合策略频率 (GTO)
  bool is_gto_best = false;  // 是否是 GTO 最优解
};

struct SpotResult {
  std::string hero_range_str;
  std::string villain_range_str;
  std::string board_str;
  double pot;
  double to_call;
  std::string position;

  std::vector<SpotAction> actions;
  SpotAction* best_action = nullptr;

  // 摘要
  double hero_ev = 0;
  double strategy_ev = 0;

  std::string ToString() const;
};

class SpotSolver {
 public:
  enum class SolveMode {
    PureExploit,    // 最优纯策略 (最大化 EV)
    MixedGTO,       // 混合 GTO 策略
    MinimumDefense  // 最小防守频率
  };

  SpotSolver();

  // 配置
  void SetHeroRange(const poker_engine::range::Range& r);
  void SetVillainRange(const poker_engine::range::Range& r);
  void SetBoard(const std::vector<std::string>& cards);
  void SetPot(double pot);
  void SetToCall(double to_call);
  void SetMode(SolveMode mode);
  void SetSamples(int samples);

  // 求解
  SpotResult Solve();

  // 快速求解 (一行调用)
  static SpotResult QuickSolve(const std::string& hero_range, const std::string& villain_range,
                               const std::string& board, double pot, double to_call);

  // 公开给测试用
  double ComputeEquity();

 private:
  poker_engine::range::Range hero_range_;
  poker_engine::range::Range villain_range_;
  std::vector<uint8_t> board_ids_;
  double pot_ = 0;
  double to_call_ = 0;
  SolveMode mode_ = SolveMode::PureExploit;
  int n_samples_ = 20000;

  double cached_equity_ = -1;

  std::vector<SpotAction> GenerateActions();
  SpotAction BestByEV(const std::vector<SpotAction>& actions);
};

}  // namespace phase3
}  // namespace poker_engine
