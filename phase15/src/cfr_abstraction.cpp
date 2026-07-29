#include "poker_engine/cfr/cfr_abstraction.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <numeric>

#include "poker_engine/base/logging.h"

namespace poker_engine::cfr {

HandAbstraction::HandAbstraction() { bucket_table_.fill(0); }

void HandAbstraction::compute_buckets(const evaluator::Evaluator& eval) {
  struct HandInfo {
    uint8_t c1, c2;
    double equity_vs_random;
    bool suited;
    int rank1, rank2;
  };

  std::vector<HandInfo> hands;

  for (int c1 = 0; c1 < kTotalCards; ++c1) {
    for (int c2 = c1 + 1; c2 < kTotalCards; ++c2) {
      if ((c1 % kRanks) == (c2 % kRanks) && (c1 / kRanks) > (c2 / kRanks)) continue;

      HandInfo hi;
      hi.c1 = c1;
      hi.c2 = c2;
      hi.suited = (c1 / kRanks) == (c2 / kRanks);

      int r1 = c1 % kRanks;
      int r2 = c2 % kRanks;
      hi.rank1 = std::max(r1, r2);
      hi.rank2 = std::min(r1, r2);
      hi.equity_vs_random = 0.0;
      hands.push_back(hi);
    }
  }

  std::sort(hands.begin(), hands.end(), [](const HandInfo& a, const HandInfo& b) {
    if (a.rank1 != b.rank1) return a.rank1 > b.rank1;
    if (a.rank2 != b.rank2) return a.rank2 > b.rank2;
    return a.suited > b.suited;
  });

  int bucket = 0;
  for (int r1 = kRanks - 1; r1 >= 0; --r1) {
    for (int r2 = kRanks - 1; r2 >= 0; --r2) {
      if (bucket >= kNumBuckets) break;
      bucket_info_[bucket] = {static_cast<uint8_t>(r1), static_cast<uint8_t>(r2), r1 != r2};
      bucket++;
    }
  }

  for (auto& hi : hands) {
    uint16_t bucket_idx = 0;
    for (int i = 0; i < kNumBuckets; ++i) {
      auto& bi = bucket_info_[i];
      bool match_rank = (hi.rank1 == bi.r1 && hi.rank2 == bi.r2);
      if (match_rank) {
        if ((hi.rank1 == hi.rank2) || (hi.suited == bi.suited)) {
          bucket_idx = i;
          break;
        }
      }
    }

    bucket_table_[hi.c1 * kTotalCards + hi.c2] = bucket_idx;
    bucket_table_[hi.c2 * kTotalCards + hi.c1] = bucket_idx;
  }

  PE_LOG_INFO("HandAbstraction: {} buckets initialized", kNumBuckets);
}

void HandAbstraction::Initialize(const evaluator::Evaluator& eval) { compute_buckets(eval); }

uint16_t HandAbstraction::get_bucket(HoleCards hand) const {
  return bucket_table_[hand.c1 * kTotalCards + hand.c2];
}

std::string HandAbstraction::bucket_name(uint16_t bucket) const {
  if (bucket >= kNumBuckets) return "??";
  auto& bi = bucket_info_[bucket < kNumBuckets ? bucket : 0];
  const char* s = bi.suited ? "s" : "o";
  const char* ranks[] = {"2", "3", "4", "5", "6", "7", "8", "9", "T", "J", "Q", "K", "A"};
  if (bi.r1 == bi.r2) s = "";
  return std::string(ranks[bi.r1]) + ranks[bi.r2] + s;
}

uint64_t InfosetKey::hash() const {
  uint64_t h = hand_bucket;
  h = h * 37 + street;
  h = h * 37 + pot_size;
  h = h * 37 + bet_sequence;
  h = h * 37 + player;
  return h;
}

}  // namespace poker_engine::cfr
