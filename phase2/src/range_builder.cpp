#include "poker_engine/phase2/range_builder.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace poker_engine {
namespace phase2 {
namespace {
using Range = poker_engine::range::Range;
}

// 6-max 预翻牌范围规范 (标准 TAG / GTO 近似)
const PreflopRangeSpec RangeBuilder::specs6max_[8] = {
    // SB
    {Position::SB, 1.00f, 0.25f,
     "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q9o+,J9o+,T8o+,97o+,"
     "86o+,75o+,64o+,53s",
     "QQ+,AKs,AQo+", "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s"},
    // BB
    {Position::BB, 1.00f, 0.35f,
     "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q9o+,J9o+,T8o+,97o+,"
     "86o+,75o+,64o+,53s",
     "QQ+,AKs,AQo+",
     "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q9o+,J9o+,T8o+,97o+,"
     "86o+,75o+,64o+,53s"},
    // UTG
    {Position::UTG, 0.45f, 0.12f, "77+,A2s+,K9s+,Q9s+,J9s+,T9s,98s,87s,76s,65s,54s,AQo+,KQo,QJo",
     "TT+,AKs,AQs,AQo+", "77+,A2s+,K9s+,Q9s+,J9s+,T9s,98s,87s,76s+54s,AQo+,KQo"},
    // UTG+1
    {Position::UTG1, 0.40f, 0.14f, "77+,A2s+,K9s+,Q9s+,J9s+,T9s,98s,87s,76s,65s,54s,AQo+,KQo,QJo",
     "TT+,AKs,AQs,AQo+", "77+,A2s+,K9s+,Q9s+,J9s+,T9s,98s,87s,76s+,AQo+,KQo"},
    // MP
    {Position::MP, 0.35f, 0.18f, "66+,A2s+,K9s+,Q9s+,J9s+,T8s+,97s+,87s,76s,65s,54s,AJo+,KQo,QJo",
     "TT+,AKs,AQs,AJs,AQo+", "66+,A2s+,K9s+,Q9s+,J9s+,T8s+,97s+,87s,76s+54s,AJo+,KQo+"},
    // MP+1
    {Position::MP1, 0.35f, 0.20f, "66+,A2s+,K9s+,Q9s+,J9s+,T8s+,97s+,87s,76s,65s,54s,AJo+,KQo,QJo",
     "TT+,AKs,AQs,AJs,AQo+", "66+,A2s+,K9s+,Q9s+,J9s+,T8s+,97s+,87s,76s+54s,AJo+,KQo+"},
    // CO
    {Position::CO, 0.45f, 0.25f,
     "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q9o+,J9o+,T8o+,97o+,"
     "86o+,75o+,64o+,53s",
     "TT+,AKs,AQs+,AJo+,KQs,QJs,AQo+",
     "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s"},
    // BTN
    {Position::BTN, 0.70f, 0.30f,
     "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+,"
     "82o+,72o+,62o+,52o+,42o+",
     "TT+,AKs,AQs+,AJo+,KQs,QJs,AQo+",
     "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,"
     "92o+"}};

const PreflopRangeSpec& RangeBuilder::Get6MaxSpec(Position pos) {
  return specs6max_[static_cast<uint8_t>(pos)];
}

Range RangeBuilder::Build(const std::string& spec_name) {
  for (int i = 0; i <= static_cast<int>(Position::_COUNT) - 1; i++) {
    if (spec_name == PositionName[i]) {
      return Range::FromString(specs6max_[i].raise_range);
    }
  }
  return Range::FromString(spec_name);
}

// ===================== ICM 计算 =====================
// Correct ICM: recursively compute each player's equity
// by considering who finishes first, then recursing on remaining players

namespace icm_detail {
// Compute ICM equity for player at index 'target' among 'n' players
// remaining[i] = chip counts for players still in, -1 for eliminated
// prob = probability of reaching this state
static void ICMRecurse(double* equity, const double remaining[], int n, const double payouts[],
                       int payout_count, int place, double prob) {
  if (place >= payout_count || place >= n) {
    // No more payouts — remaining players get 0
    return;
  }

  double total = 0;
  for (int i = 0; i < n; i++) {
    if (remaining[i] > 0) total += remaining[i];
  }
  if (total <= 0) return;

  for (int i = 0; i < n; i++) {
    if (remaining[i] <= 0) continue;
    double p_first = remaining[i] / total;
    equity[i] += prob * p_first * payouts[place];

    // Recurse: player i eliminated
    double saved = remaining[i];
    double rem_copy[4];
    for (int j = 0; j < n; j++) rem_copy[j] = remaining[j];
    rem_copy[i] = 0;

    ICMRecurse(equity, rem_copy, n, payouts, payout_count, place + 1, prob * p_first);
  }
}
}  // namespace icm_detail

void RangeBuilder::ICMRecursive(double* equity, const double chips[], int n, double sum_chips,
                                const double payouts[], int payout_count, int depth,
                                double* remaining_chips, int* order) {
  // Not used — see icm_detail::ICMRecurse
}

double RangeBuilder::ICMEquityForPlace(const double chips[], int n, int target_place,
                                       const double payouts[], int payout_count) {
  double equity[4] = {0};
  double remaining[4] = {0};
  for (int i = 0; i < n && i < 4; i++) remaining[i] = chips[i];
  icm_detail::ICMRecurse(equity, remaining, n, payouts, payout_count, 0, 1.0);
  if (target_place < 4) return equity[target_place];
  return 0;
}

RangeBuilder::ICMResult RangeBuilder::CalculateICM(const double payouts[], int payout_count,
                                                   const double chips[], int player_count,
                                                   double big_blind) {
  ICMResult result;
  int n = std::min(player_count, 4);

  double equity[4] = {0};
  double remaining[4] = {0};
  for (int i = 0; i < n; i++) remaining[i] = chips[i];

  icm_detail::ICMRecurse(equity, remaining, n, payouts, payout_count, 0, 1.0);

  // Normalize to 0~1 (divide by total payout)
  double total_payout = 0;
  for (int i = 0; i < payout_count; i++) total_payout += payouts[i];

  for (int i = 0; i < n; i++) {
    result.equity[i] = total_payout > 0 ? equity[i] / total_payout : 0;
    result.bubble_factor[i] = 1.0;
    result.m_zone_stack[i] = chips[i] / big_blind;
  }

  return result;
}

Range RangeBuilder::AdjustForHeadsUp(const Range& range, bool is_aggressor) { return range; }

}  // namespace phase2
}  // namespace poker_engine
