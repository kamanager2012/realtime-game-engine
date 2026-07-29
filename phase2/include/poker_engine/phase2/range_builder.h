#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase2 {

// ===================== 位置枚举 =====================
enum class Position : uint8_t { SB = 0, BB, UTG, UTG1, MP, MP1, CO, BTN, _COUNT };

static constexpr const char* PositionName[] = {"SB", "BB",   "UTG", "UTG+1",
                                               "MP", "MP+1", "CO",  "BTN"};

// ===================== 预翻牌范围定义 =====================
// 6-max 标准范围 (压缩表示)
struct PreflopRangeSpec {
  Position pos;
  double raise_pct;      // 开放加注频率 (0~1)
  double three_bet_pct;  // 3-bet 频率 (vs 开放加注)
  std::string raise_range;
  std::string three_bet_range;
  std::string cold_call_range;
};

class RangeBuilder {
 public:
  RangeBuilder() = default;

  // 获取 6-max 各位置标准范围
  static const PreflopRangeSpec& Get6MaxSpec(Position pos);

  // 构建预定义范围
  poker_engine::range::Range Build(const std::string& spec_name);

  // ICM 相关
  struct ICMResult {
    double equity[4];         // 4 名选手各自的 Equity (筹码比例 → 奖金期望)
    double bubble_factor[4];  // 每个选手的泡沫系数
    double m_zone_stack[4];   // M 值 (BB 倍数)
  };

  // 简化 ICM 计算 (Independent Chip Model)
  // payouts: 各名次奖金, chips: 各选手当前筹码
  static ICMResult CalculateICM(const double payouts[], int payout_count, const double chips[],
                                int player_count, double big_blind);

  // 单挑 (HU) 范围调整
  static poker_engine::range::Range AdjustForHeadsUp(const poker_engine::range::Range& range,
                                                     bool is_aggressor);

 private:
  static const PreflopRangeSpec specs6max_[8];

  // ICM 辅助: 计算单一名次的概率
  static double ICMEquityForPlace(const double chips[], int n, int target_place,
                                  const double payouts[], int payout_count);

  // 递归 ICM 排列计算
  static void ICMRecursive(double* equity, const double chips[], int n, double sum_chips,
                           const double payouts[], int payout_count, int depth,
                           double* remaining_chips, int* order);
};

}  // namespace phase2
}  // namespace poker_engine
