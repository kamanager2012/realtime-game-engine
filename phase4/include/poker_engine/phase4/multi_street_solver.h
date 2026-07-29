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
namespace phase4 {

// ========== 多街 CFRA 求解器 ==========

inline double MS_PositivePart(double x) { return x > 0 ? x : 0; }

enum class MS_Action : uint8_t { FOLD = 0, CHECK, CALL, BET_POT, ALL_IN, _COUNT };

static constexpr int NUM_MS_ACTIONS = static_cast<int>(MS_Action::_COUNT);
static constexpr const char* MS_ActionName[] = {"FOLD", "CHECK", "CALL", "BET_POT", "ALL_IN"};

struct MS_InfoSetKey {
  int street;
  std::string history;  // 压缩的历史行动序列

  bool operator<(const MS_InfoSetKey& o) const {
    if (street != o.street) return street < o.street;
    return history < o.history;
  }
};

struct MS_Node {
  MS_InfoSetKey key;
  double cumulative_regret[NUM_MS_ACTIONS];
  double strategy_sum[NUM_MS_ACTIONS];
  int visit_count = 0;

  MS_Node();
  void GetStrategy(double out[NUM_MS_ACTIONS]) const;
  void GetAverageStrategy(double out[NUM_MS_ACTIONS]) const;
  std::string BestActionName() const;
};

struct MS_SolveResult {
  std::map<MS_InfoSetKey, std::array<double, NUM_MS_ACTIONS>> strategies;
  double exploitability = 0;
  int iterations = 0;
  std::map<int, double> street_ev;  // 每街的 EV

  std::string ToString() const;
};

// 求解器配置
struct MS_Config {
  int iterations = 500;
  int mc_samples = 2000;
  double discount_factor = 1.0;
  double convergence_threshold = 0.01;
  bool verbose = false;
};

class MultiStreetSolver {
 public:
  explicit MultiStreetSolver(const MS_Config& config = MS_Config());

  // 设置范围
  void SetRanges(const std::vector<poker_engine::range::Range>& ranges);

  // 设置公共牌 (逐步揭示)
  void SetFlop(const std::vector<poker_engine::Card>& flop);
  void SetTurn(poker_engine::Card turn);
  void SetRiver(poker_engine::Card river);

  // 从牌局历史求解
  MS_SolveResult SolveFromHand(const poker_engine::phase4::HandHistory& hh);

  // 完整求解 (从头开始)
  MS_SolveResult SolveFullTree();

  // 获取特定信息集的策略
  std::array<double, NUM_MS_ACTIONS> GetStrategy(const MS_InfoSetKey& key) const;

  // 工具
  static std::string ActionToString(MS_Action a);

 private:
  MS_Config config_;
  std::vector<poker_engine::range::Range> ranges_;
  std::vector<poker_engine::Card> board_;
  std::map<MS_InfoSetKey, MS_Node> node_map_;
  std::mt19937 rng_{42};

  // 核心递归
  double CFR(int player, double reach_probs[4], int depth, int street_idx,
             const std::string& history);

  // 工具
  static double PositivePart(double x) { return MS_PositivePart(x); }
  double ComputeEquity(int player, int street_idx, int n_samples);
};

// ========== 策略可视化 ==========
struct StrategyVisualization {
  static std::string PrintNode(const MS_InfoSetKey& key,
                               const std::array<double, NUM_MS_ACTIONS>& strategy);
  static std::string PrintAllNodes(
      const std::map<MS_InfoSetKey, std::array<double, NUM_MS_ACTIONS>>& strategies);
};

}  // namespace phase4
}  // namespace poker_engine
