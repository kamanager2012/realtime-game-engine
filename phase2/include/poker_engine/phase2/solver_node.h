#pragma once
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/phase2/range_builder.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase2 {

// ===================== 动作抽象 =====================
enum class Action : uint8_t {
  FOLD = 0,
  CHECK,
  CALL,
  BET_33,  // 底池 1/3
  BET_67,  // 底池 2/3
  POT,     // 全底池
  ALL_IN,
  _COUNT
};

static constexpr const char* ActionName[] = {"FOLD",   "CHECK", "CALL",  "BET_33",
                                             "BET_67", "POT",   "ALL_IN"};
static constexpr int NUM_ACTIONS = static_cast<int>(Action::_COUNT);

// ===================== 信息集 =====================
struct InfoSet {
  std::string key;  // 唯一标识 (如 "flop_QdJc7h_p0")
  int street;       // 0=preflop, 1=flop, 2=turn, 3=river
  double pot;
  double to_call;

  bool operator==(const InfoSet& o) const { return key == o.key; }
  bool operator<(const InfoSet& o) const { return key < o.key; }
};

// 为 InfoSet 提供 hash
struct InfoSetHash {
  size_t operator()(const InfoSet& is) const {
    size_t h = 0;
    for (char c : is.key) h = h * 31 + c;
    return h;
  }
};

// ===================== 博弈树节点 =====================
struct CFranode {
  InfoSet info_set;
  double cumulative_regret[NUM_ACTIONS];  // 累积反悔值
  double strategy_sum[NUM_ACTIONS];       // 策略累加 (用于计算平均策略)
  int visit_count = 0;

  CFranode();

  // 获取当前策略 (基于 regret matching)
  void GetStrategy(double out_strategy[NUM_ACTIONS]) const;

  // 获取平均策略
  void GetAverageStrategy(double out_strategy[NUM_ACTIONS]) const;

  // 选择动作 (基于策略采样)
  Action SampleAction(double strategy[NUM_ACTIONS], double roll) const;

  std::string BestAction() const;
};

// ===================== CFRA 求解器 =====================
struct SolverConfig {
  int iterations = 2000;  // 迭代次数
  int n_samples = 5000;   // 每次迭代的 MC 采样数
  double discount = 1.0;  // 反悔值折扣因子
  bool verbose = false;
};

using RangeVector = std::vector<poker_engine::range::Range>;

// 求解结果
struct SolveResult {
  std::map<InfoSet, std::array<double, NUM_ACTIONS>> strategy_profile;
  double exploitability = 0;
  int iterations = 0;
  double total_ev = 0;

  std::string ToString() const;
  std::string FormatNode(const InfoSet& is) const;
};

class CFRSolver {
 public:
  CFRSolver(const SolverConfig& config);

  // 设置牌局范围
  void SetRanges(const RangeVector& ranges);
  void SetBoard(const std::vector<Card>& board, int street);

  // 主求解循环
  SolveResult Solve();

  // 获取特定信息集的策略
  std::array<double, NUM_ACTIONS> GetStrategy(const InfoSet& is) const;

 private:
  SolverConfig config_;
  RangeVector ranges_;
  std::vector<Card> board_;
  std::map<InfoSet, CFranode> node_map_;

  // 递归求解
  double CFR(int player, double reach_probs[4], int depth, int street_idx);

  // 工具
  static double PositivePart(double x) { return x > 0 ? x : 0; }
};

}  // namespace phase2
}  // namespace poker_engine
