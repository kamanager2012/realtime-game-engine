#pragma once
#include <string>
#include <vector>

#include "poker_engine/range/range.h"
#include "poker_engine/range/suit_iso.h"

namespace poker_engine {
namespace phase2 {

class RangeVisualizer {
 public:
  enum class HandType { Pairs = 0, Suited, Offsuit };

  // 构造函数
  explicit RangeVisualizer(const poker_engine::range::Range& range);

  // ASCII 热力图 - 给定手牌类别的所有组合
  std::string ToASCII(HandType type) const;

  // 完整 ASCII 报告 (3 个面板: 对子/同花/杂色)
  std::string ToFullASCII() const;

  // HTML 热力图 (彩色表格)
  std::string ToHTML() const;

  // 获取指定抽象手牌的频率
  double GetFrequency(poker_engine::AbstractId id) const;

 private:
  const poker_engine::range::Range& range_;

  // 辅助: 生成颜色渐变
  static std::string FrequencyToColor(double freq);
  static std::string FrequencyToChar(double freq);
};

}  // namespace phase2
}  // namespace poker_engine
