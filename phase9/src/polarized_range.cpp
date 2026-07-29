#include "poker_engine/phase9/polarized_range.h"

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/range/hand_id.h"

namespace poker_engine {
namespace phase9 {
using namespace poker_engine::range;
using poker_engine::Card;
using poker_engine::equity::EquityCalculator;
using poker_engine::range::HandId;

std::string PolarizationResult::ToString() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(2);
  o << "Value: " << value_hands.NonZeroCount() << " | Bluffs: " << bluffs.NonZeroCount();
  o << " | Merged: " << merged_alternative.NonZeroCount() << "\n";
  o << "V%: " << value_freq * 100 << " | B%: " << bluff_freq * 100 << " | M%: " << merged_freq * 100
    << "\n";
  o << "Top eq: " << top_equity * 100 << "% | Avg eq: " << avg_equity * 100 << "%\n";
  return o.str();
}

PolarizationResult PolarizedRangeBuilder::BuildPolarized(const Range& base,
                                                         const std::vector<Card>& board,
                                                         const PolarizationConfig& cfg) {
  PolarizationResult r;
  int bs = static_cast<int>(board.size());
  uint8_t b5[5] = {0};
  for (int i = 0; i < bs && i < 5; i++) b5[i] = board[i].Id();

  std::vector<std::pair<int, double>> eqs;
  for (int i = 0; i < 1326; i++) {
    float w = base.Get(i);
    if (w <= 0) continue;
    Range s;
    s.Set(static_cast<uint16_t>(i), 1.0f);
    double eq = 0.5;
    if (bs > 0) {
      std::mt19937 rng(42 + i);
      auto res = EquityCalculator::CalculateMonteCarlo(s, base, b5, bs,
                                                       std::max(500, cfg.board_size * 100), rng);
      eq = res.equity[0];
    }
    eqs.push_back({i, eq});
  }
  if (eqs.empty()) return r;

  std::sort(eqs.begin(), eqs.end(), [](auto& a, auto& b) { return a.second > b.second; });
  int nh = static_cast<int>(eqs.size());

  // If no board (all eq=0.5), use frequency-based split
  bool no_board = (bs == 0);
  int n_value = std::max(1, static_cast<int>(nh * cfg.top_hands_pct));
  int n_bluff = std::max(1, static_cast<int>(nh * cfg.bluff_ratio));

  for (int idx = 0; idx < nh; idx++) {
    auto& [i, eq] = eqs[idx];
    float w = base.Get(i);
    if (no_board) {
      // Frequency-based: top hands = value, bottom hands = bluff
      if (idx < n_value) {
        r.value_hands.Set(static_cast<uint16_t>(i), w);
      } else if (idx >= nh - n_bluff) {
        r.bluffs.Set(static_cast<uint16_t>(i), w);
      }
    } else {
      // Equity-based classification
      if (eq >= cfg.equity_threshold_high)
        r.value_hands.Set(static_cast<uint16_t>(i), w);
      else if (eq <= cfg.equity_threshold_low)
        r.bluffs.Set(static_cast<uint16_t>(i), w);
    }
    double mw = w * (0.5 + (eq - 0.5) * 0.8);
    r.merged_alternative.Set(static_cast<uint16_t>(i), static_cast<float>(mw));
  }

  int nb = base.NonZeroCount();
  r.value_freq = nb > 0 ? static_cast<double>(r.value_hands.NonZeroCount()) / nb : 0;
  r.bluff_freq = nb > 0 ? static_cast<double>(r.bluffs.NonZeroCount()) / nb : 0;
  r.merged_freq = nb > 0 ? static_cast<double>(r.merged_alternative.NonZeroCount()) / nb : 0;
  r.top_equity = nh > 0 ? eqs[0].second : 0;
  r.avg_equity = nh > 0 ? std::accumulate(eqs.begin(), eqs.end(), 0.0,
                                          [](double s, auto& e) { return s + e.second; }) /
                              nh
                        : 0.5;
  return r;
}

PolarizationResult PolarizedRangeBuilder::BuildMerged(const Range& base,
                                                      const std::vector<Card>& board) {
  PolarizationConfig cfg;
  cfg.top_hands_pct = 0.15;
  cfg.bluff_ratio = 0.10;
  return BuildPolarized(base, board, cfg);
}

PolarizationResult PolarizedRangeBuilder::BuildFromString(const std::string& rs,
                                                          const std::string& bs, Polarity pol) {
  Range base = Range::FromString(rs);
  std::vector<Card> board;
  for (size_t i = 0; i + 1 < bs.size(); i += 2) board.push_back(Card::Parse(bs.substr(i, 2)));

  PolarizationConfig cfg;
  switch (pol) {
    case Polarity::POLARIZED:
      cfg.top_hands_pct = 0.15;
      cfg.bluff_ratio = 0.25;
      cfg.equity_threshold_high = 0.65;
      cfg.equity_threshold_low = 0.35;
      break;
    case Polarity::MERGED:
      return BuildMerged(base, board);
    case Polarity::CAPPED:
      cfg.top_hands_pct = 0.05;
      cfg.bluff_ratio = 0.10;
      cfg.equity_threshold_high = 0.55;
      cfg.equity_threshold_low = 0.35;
      break;
    case Polarity::SUPER_POLARIZED:
      cfg.top_hands_pct = 0.20;
      cfg.bluff_ratio = 0.35;
      cfg.equity_threshold_high = 0.55;
      cfg.equity_threshold_low = 0.40;
      break;
  }
  return BuildPolarized(base, board, cfg);
}

Range PolarizedRangeBuilder::Intersect(const Range& a, const Range& b) {
  Range r;
  for (int i = 0; i < 1326; i++) {
    float w = std::min(a.Get(i), b.Get(i));
    if (w > 0) r.Set(static_cast<uint16_t>(i), w);
  }
  return r;
}
Range PolarizedRangeBuilder::Exclude(const Range& a, const Range& b) {
  Range r;
  for (int i = 0; i < 1326; i++) {
    float w = a.Get(i);
    if (w > 0 && b.Get(i) <= 0) r.Set(static_cast<uint16_t>(i), w);
  }
  return r;
}
Range PolarizedRangeBuilder::Merge(const Range& a, const Range& b) {
  Range r;
  for (int i = 0; i < 1326; i++) r.Set(static_cast<uint16_t>(i), std::max(a.Get(i), b.Get(i)));
  return r;
}

double PolarizedRangeBuilder::RangeOverlap(const Range& a, const Range& b) {
  int sh = 0, tot = 0;
  for (int i = 0; i < 1326; i++) {
    bool ia = a.Get(i) > 0, ib = b.Get(i) > 0;
    if (ia || ib) tot++;
    if (ia && ib) sh++;
  }
  return tot > 0 ? static_cast<double>(sh) / tot : 0;
}

std::string PolarizedRangeBuilder::RangeComparison(const Range& p, const Range& m) {
  int pc = p.NonZeroCount(), mc = m.NonZeroCount(), ov = 0;
  for (int i = 0; i < 1326; i++)
    if (p.Get(i) > 0 && m.Get(i) > 0) ov++;
  std::ostringstream o;
  o << "Polarized: " << pc << " | Merged: " << mc << " | Overlap: " << ov << "/" << std::max(pc, mc)
    << "\n";
  return o.str();
}

}  // namespace phase9
}  // namespace poker_engine
