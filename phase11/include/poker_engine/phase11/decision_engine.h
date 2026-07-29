#pragma once
#include <memory>
#include <string>
#include <vector>

#include "poker_engine/phase11/fast_preflop_solver.h"
#include "poker_engine/phase5/equity_matrix.h"
#include "poker_engine/phase8/exploit_engine.h"
#include "poker_engine/phase8/mc_cfr_deep.h"

namespace poker_engine {
namespace phase11 {

// ========== 实时决策引擎 ==========
// 统一入口: 根据当前局面选择最佳求解方法

enum class DecisionLevel {
  QUICK,     // 快速估算 (LUT, <1s)
  STANDARD,  // 标准求解 (MC-CFR, ~10s)
  DEEP,      // 深度求解 (完整CFR, ~30s+)
  RESEARCH   // 研究模式 (大量迭代, ~60s+)
};

struct GameContext {
  std::string hero_cards;
  std::vector<std::string> community_cards;
  double pot;
  double to_call;
  int street;    // 0=preflop, 1=flop, 2=turn, 3=river
  int position;  // 0=UTG ... 8=BB
  double big_blind;
  double effective_stack;
  std::string villain_tendency;  // "tight", "medium", "loose", "aggressive"
  int num_players;
};

struct DecisionResult {
  std::string recommended_action;
  double confidence;                                      // 0-1
  double expected_value;                                  // in BB
  std::vector<std::pair<std::string, double>> action_ev;  // all actions with EVs
  double equity;                                          // hero equity
  double exploitability;                                  // mBB
  std::string reasoning;
  double compute_time_ms;

  std::string ToString() const;
};

class DecisionEngine {
 public:
  DecisionEngine(DecisionLevel level = DecisionLevel::STANDARD);

  // 主决策入口
  DecisionResult Decide(const GameContext& context);

  // 可选的: 设置已知对手策略
  void SetVillainRange(const std::string& range_str);

  // 设置求解级别
  void SetLevel(DecisionLevel level);

  // 重置求解器状态
  void Reset();

 private:
  DecisionLevel level_;
  std::string villain_range_;
  std::unique_ptr<poker_engine::phase8::MCCRDeepSolver> mc_solver_;
  std::unique_ptr<FastPreflopSolver> fast_solver_;

  DecisionResult DecidePreFlop(const GameContext& ctx);
  DecisionResult DecidePostFlop(const GameContext& ctx);
  DecisionResult QuickDecision(const GameContext& ctx);
};

}  // namespace phase11
}  // namespace poker_engine
