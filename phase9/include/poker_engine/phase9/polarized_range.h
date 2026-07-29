#pragma once
#include <string>
#include <vector>

#include "poker_engine/evaluator/card.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase9 {

enum class Polarity { POLARIZED, MERGED, CAPPED, SUPER_POLARIZED };

struct PolarizationConfig {
  double equity_threshold_high = 0.65;
  double equity_threshold_low = 0.35;
  double bluff_ratio = 0.25;
  double top_hands_pct = 0.15;
  int board_size = 0;
};

struct PolarizationResult {
  poker_engine::range::Range polarized_range;
  poker_engine::range::Range value_hands;
  poker_engine::range::Range bluffs;
  poker_engine::range::Range merged_alternative;
  double value_freq = 0;
  double bluff_freq = 0;
  double merged_freq = 0;
  double top_equity = 0;
  double avg_equity = 0;
  std::string ToString() const;
};

class PolarizedRangeBuilder {
 public:
  static PolarizationResult BuildPolarized(const poker_engine::range::Range& base_range,
                                           const std::vector<poker_engine::Card>& board,
                                           const PolarizationConfig& config = PolarizationConfig{});

  static PolarizationResult BuildMerged(const poker_engine::range::Range& base_range,
                                        const std::vector<poker_engine::Card>& board);

  static PolarizationResult BuildFromString(const std::string& range_str,
                                            const std::string& board_str = "",
                                            Polarity polarity = Polarity::POLARIZED);

  static poker_engine::range::Range Intersect(const poker_engine::range::Range& a,
                                              const poker_engine::range::Range& b);
  static poker_engine::range::Range Exclude(const poker_engine::range::Range& a,
                                            const poker_engine::range::Range& b);
  static poker_engine::range::Range Merge(const poker_engine::range::Range& a,
                                          const poker_engine::range::Range& b);
  static double RangeOverlap(const poker_engine::range::Range& a,
                             const poker_engine::range::Range& b);
  static std::string RangeComparison(const poker_engine::range::Range& polarized,
                                     const poker_engine::range::Range& merged);
};

}  // namespace phase9
}  // namespace poker_engine
