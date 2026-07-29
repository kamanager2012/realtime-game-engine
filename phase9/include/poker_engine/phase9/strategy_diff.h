#pragma once
#include <map>
#include <string>
#include <vector>

#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase9 {

struct DiffEntry {
  std::string hand_name;
  double freq_a = 0;
  double freq_b = 0;
  double diff = 0;
  std::string category;
  std::string classification;
  std::string ToString() const;
};

struct DiffSummary {
  int total_hands = 0;
  int big_diffs = 0;
  int medium_diffs = 0;
  int close_matches = 0;
  double avg_abs_diff = 0;
  double max_diff = 0;
  std::string max_diff_hand;
  std::map<std::string, int> diff_by_category;
  std::map<std::string, double> avg_diff_by_category;
  std::string ToString() const;
};

class StrategyDiffAnalyzer {
 public:
  StrategyDiffAnalyzer();

  DiffSummary CompareRanges(const poker_engine::range::Range& range_a,
                            const poker_engine::range::Range& range_b, double threshold_high = 0.15,
                            double threshold_medium = 0.05);

  DiffSummary CompareByPosition(
      const std::map<std::string, poker_engine::range::Range>& range_by_position);

  static std::string ClassifyHandDiff(double diff);
};

}  // namespace phase9
}  // namespace poker_engine
