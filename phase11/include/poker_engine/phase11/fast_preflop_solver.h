#pragma once
#include <map>
#include <string>
#include <vector>

#include "poker_engine/phase10/parallel_preflop_lut.h"
#include "poker_engine/phase8/preflop_solver.h"

namespace poker_engine {
namespace phase11 {

using namespace poker_engine::phase10;

// ========== LUT加速翻前求解器 ==========
// 使用预计算LUT替代MC采样，速度提升50-100x

enum class FastSolveMode {
  LUT_ONLY,    // 纯LUT查表 (最快, 略粗糙)
  LUT_REFINE,  // LUT初筛 + 少次MC精炼
  HYBRID       // 混合: LUT指导 + 自适应采样
};

struct FastPreflopConfig {
  FastSolveMode mode = FastSolveMode::LUT_REFINE;
  int mc_refine_samples = 200;  // LUT_REFINE模式下的精炼样本数
  int lut_min_samples = 500;    // LUT最小样本数
  bool verbose = false;
};

struct FastPreflopResult {
  struct HandAdvice {
    std::string hand_name;
    float equity_vs_1bb_range;
    float ev_vs_1bb;
    float optimal_raise_pct;  // 最优加注频率
    float optimal_3bet_pct;   // 最优3bet频率
    std::string category;     // "Always Raise", "Mix", "Call/Fold", "Always Fold"
    float score;              // 综合评分 (0-100)
  };

  std::vector<HandAdvice> advice;  // 按评分排序
  double solve_time_ms = 0;
  int hands_evaluated = 0;
  FastSolveMode mode_used;

  std::string ToString() const;
  std::vector<HandAdvice> TopN(int n) const;
};

struct PositionVars {
  double vpip;       // 平均VPIP (0-1)
  double pfr;        // 平均PFR (0-1)
  double three_bet;  // 平均3bet率 (0-1)
};

class FastPreflopSolver {
 public:
  explicit FastPreflopSolver(const FastPreflopConfig& config = FastPreflopConfig());

  // 加载LUT (在求解前必须调用)
  bool LoadLUT(const std::string& filepath);

  // 如果LUT不存在, 则构建 (较慢, 只需一次)
  bool BuildLUT(const std::string& save_path);

  // 求解特定位置的翻前策略
  FastPreflopResult SolvePosition(const std::string& position,
                                  const PositionVars& opponent_profile);

  // 快速分析单手 (用于实时决策)
  FastPreflopResult::HandAdvice AnalyzeSingleHand(const std::string& hand,
                                                  const std::string& position,
                                                  const PositionVars& opponent_profile);

  // 批量求解所有位置
  std::map<std::string, FastPreflopResult> SolveAllPositions();

  // 比较两个策略
  FastPreflopResult CompareStrategies(const std::string& hand_range_a,
                                      const std::string& hand_range_b);

 private:
  FastPreflopConfig config_;
  bool lut_loaded_ = false;
  PreflopLUT lut_;
  std::string position_;

  double ComputePositionThreshold(const std::string& position) const;
  FastPreflopResult::HandAdvice ScoreHand(int hand_type_idx, const PositionVars& opp) const;
  std::string ClassifyAction(float raise_pct, float call_pct) const;
};

}  // namespace phase11
}  // namespace poker_engine
