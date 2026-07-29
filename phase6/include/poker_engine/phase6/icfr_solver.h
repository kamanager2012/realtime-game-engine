#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/phase4/hh_parser.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase6 {

// ========== Imitation CFR (ICFR) 最佳应对求解器 ==========
// 给定对手的观察策略 (蓝图), 求解 Hero 的最佳应对策略

enum class ICAction : uint8_t { FOLD = 0, CHECK, CALL, BET_50, BET_POT, ALL_IN, _COUNT };

static constexpr int NUM_IC_ACTIONS = static_cast<int>(ICAction::_COUNT);
static constexpr const char* ICActionName[] = {"FOLD",    "CHECK",   "CALL",
                                               "BET_50%", "BET_POT", "ALL_IN"};

// 对手蓝图策略: 信息集 → 各动作概率
using BlueprintStrategy = std::map<std::string, std::array<double, NUM_IC_ACTIONS>>;

struct ICInfoSetKey {
  int street = 0;       // 0=preflop, 1=flop, 2=turn, 3=river
  std::string history;  // 压缩动作历史
  double pot = 0;
  double to_call = 0;

  std::string Key() const {
    return std::to_string(street) + ":" + history + ":p" +
           std::to_string(static_cast<int>(pot * 100)) + ":c" +
           std::to_string(static_cast<int>(to_call * 100));
  }

  bool operator<(const ICInfoSetKey& o) const { return Key() < o.Key(); }
};

struct ICNode {
  double cumulative_regret[NUM_IC_ACTIONS];
  double strategy_sum[NUM_IC_ACTIONS];
  int visit_count = 0;

  ICNode();
  void GetStrategy(double out[NUM_IC_ACTIONS]) const;
  void GetAverageStrategy(double out[NUM_IC_ACTIONS]) const;
  std::string BestAction() const;
};

struct ICBreakdownEntry {
  std::string info_set;
  std::string recommended;
  std::array<double, NUM_IC_ACTIONS> strategy;
  double ev_if_fold = 0;
  double ev_if_play = 0;
};

struct ICFRResult {
  std::map<ICInfoSetKey, std::array<double, NUM_IC_ACTIONS>> strategy_profile;
  double exploitability_vs_blueprint = 0;  // mBB
  double achieved_ev = 0;
  int iterations = 0;
  std::vector<ICBreakdownEntry> breakdown;

  std::string ToString() const;
};

struct ICFRConfig {
  int iterations = 1000;
  int mc_samples = 2000;
  double discount = 1.0;
  double blueprint_weight = 0.5;  // 混合蓝图 vs uniform
  bool verbose = false;
};

class ICFRSolver {
 public:
  explicit ICFRSolver(const ICFRConfig& config = ICFRConfig());

  // 设置 Hero 和 Villain 范围
  void SetHeroRange(const poker_engine::range::Range& r);
  void SetVillainRange(const poker_engine::range::Range& r);

  // 设置对手蓝图 (从观察数据提取)
  void SetOpponentBlueprint(const BlueprintStrategy& bp);
  void LearnBlueprintFromHistory(const std::vector<poker_engine::phase4::HandHistory>& hands,
                                 const std::string& villain_name);

  // 设置公共牌
  void SetBoard(const std::vector<poker_engine::Card>& board);

  // 求解 Hero 最佳应对
  ICFRResult Solve();

  // 获取特定信息集策略
  std::array<double, NUM_IC_ACTIONS> GetStrategy(const ICInfoSetKey& key) const;

  // 估算 vs 蓝图的 exploitability
  double ComputeExploitability(const ICFRResult& result);

 private:
  ICFRConfig config_;
  poker_engine::range::Range hero_range_;
  poker_engine::range::Range villain_range_;
  BlueprintStrategy blueprint_;
  std::vector<poker_engine::Card> board_;
  std::map<ICInfoSetKey, ICNode> node_map_;
  std::mt19937 rng_{42};
  bool ranges_initialized_ = false;

  // 核心递归
  double ICFR(int hero_player, double reach_hero, double reach_villain, int street_idx,
              const std::string& history);

  double ComputeEquity(int player, int street_idx);
  double ComputeBetEfficiency(const std::string& history) const;
};

}  // namespace phase6
}  // namespace poker_engine
