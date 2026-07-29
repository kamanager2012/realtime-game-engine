#include "poker_engine/phase9/strategy_diff.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "poker_engine/evaluator/card.h"
#include "poker_engine/range/hand_id.h"

namespace poker_engine {
namespace phase9 {
using namespace poker_engine::range;
using poker_engine::Card;
using poker_engine::range::HandId;

std::string DiffEntry::ToString() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(1);
  o << std::setw(6) << hand_name << " | A:" << int(freq_a * 100 + 0.5)
    << "% B:" << int(freq_b * 100 + 0.5) << "% Δ:" << int(diff * 100 + 0.5) << "% ["
    << classification << "]";
  return o.str();
}

std::string DiffSummary::ToString() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(2);
  o << "=== Strategy Diff ===\n";
  o << "Total: " << total_hands << " | Big: " << big_diffs << " | Med: " << medium_diffs
    << " | Close: " << close_matches << "\n";
  o << "Avg |Δ|: " << avg_abs_diff * 100 << "% | Max: " << max_diff * 100 << "% (" << max_diff_hand
    << ")\n";
  return o.str();
}

StrategyDiffAnalyzer::StrategyDiffAnalyzer() = default;

std::string StrategyDiffAnalyzer::ClassifyHandDiff(double diff) {
  double ad = std::abs(diff);
  if (ad > 0.15) return diff > 0 ? "MORE_A" : "MORE_B";
  if (ad > 0.05) return diff > 0 ? "SLIGHTLY_A" : "SLIGHTLY_B";
  return "SAME";
}

DiffSummary StrategyDiffAnalyzer::CompareRanges(const Range& ra, const Range& rb, double th,
                                                double tm) {
  DiffSummary s;
  for (int i = 0; i < 1326; i++) {
    float wa = ra.Get(i), wb = rb.Get(i);
    if (wa <= 0 && wb <= 0) continue;
    s.total_hands++;
    double diff = static_cast<double>(wa - wb), ad = std::abs(diff);
    if (ad > th)
      s.big_diffs++;
    else if (ad > tm)
      s.medium_diffs++;
    else
      s.close_matches++;
    s.avg_abs_diff += ad;
    if (ad > s.max_diff) {
      s.max_diff = ad;
      auto [c1, c2] = HandId::Decode(static_cast<uint16_t>(i));
      s.max_diff_hand = Card(c1).ToString() + Card(c2).ToString();
    }
  }
  if (s.total_hands > 0) s.avg_abs_diff /= s.total_hands;
  return s;
}

DiffSummary StrategyDiffAnalyzer::CompareByPosition(const std::map<std::string, Range>& ranges) {
  DiffSummary s;
  if (ranges.size() < 2) return s;
  auto it = ranges.begin(), prev = it++;
  for (; it != ranges.end(); ++it, ++prev) {
    auto sub = CompareRanges(prev->second, it->second);
    s.total_hands += sub.total_hands;
    s.big_diffs += sub.big_diffs;
    s.medium_diffs += sub.medium_diffs;
    s.close_matches += sub.close_matches;
    s.avg_abs_diff += sub.avg_abs_diff * sub.total_hands;
    if (sub.max_diff > s.max_diff) {
      s.max_diff = sub.max_diff;
      s.max_diff_hand = sub.max_diff_hand;
    }
  }
  if (s.total_hands > 0) s.avg_abs_diff /= s.total_hands;
  return s;
}

}  // namespace phase9
}  // namespace poker_engine
