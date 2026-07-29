#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase3 {

// 翻牌分析结果
struct FlopAnalysis {
  std::string flop_str;   // 翻牌字符串
  double hero_equity;     // Hero 胜率
  double villain_equity;  // Villain 胜率
  double tie_pct;         // 平局率
  int hero_wins = 0, villain_wins = 0, ties = 0;

  std::string ToString() const;
};

// 转牌/河牌分析
struct RunoutStats {
  std::array<int, 52> hero_wins_by_card = {};  // indexed by card ID (0-51), 52 - flop count max
  int total_runouts = 0;
  double avg_equity = 0;
  double equity_std = 0;
};

// 翻牌探索器
class FlopExplorer {
 public:
  FlopExplorer();

  // 设置 Hero 范围和对手范围
  void SetHeroRange(const poker_engine::range::Range& r);
  void SetVillainRange(const poker_engine::range::Range& r);

  // 分析给定翻牌
  FlopAnalysis AnalyzeFlop(const std::string& flop_cards, int n_samples = 50000);

  // 分析所有两牌翻牌组合 (C(13,2)*6 + 13*4 = 1236 种翻牌)
  std::vector<FlopAnalysis> AnalyzeAllFlops(int n_samples = 10000);

  // 按花色/连张/对子分类统计翻牌
  void AnalyzeFlopCategories(int n_samples = 20000);

  // 转牌深度分析
  RunoutStats AnalyzeTurnRunouts(const std::string& flop_cards, int n_samples = 10000);

  // 分类工具方法 (公开)
  static std::string CategoryName(const std::string& flop);
  static bool IsPairedFlop(const std::string& flop);
  static bool IsMonotoneFlop(const std::string& flop);
  static bool IsConnectedFlop(const std::string& flop);

 private:
  poker_engine::range::Range hero_range_;
  poker_engine::range::Range villain_range_;
};

}  // namespace phase3
}  // namespace poker_engine
