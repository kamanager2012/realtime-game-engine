#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "types.h"

namespace poker_engine::cfr {

class HandAbstraction {
 public:
  HandAbstraction();

  void Initialize(const evaluator::Evaluator& eval);

  uint16_t get_bucket(HoleCards hand) const;

  std::string bucket_name(uint16_t bucket) const;

  constexpr static int kNumBuckets = 169;

 private:
  std::array<uint16_t, kTotalCards * kTotalCards> bucket_table_;

  struct BucketInfo {
    uint8_t r1, r2;
    bool suited;
  };
  std::array<BucketInfo, kNumBuckets> bucket_info_;

  void compute_buckets(const evaluator::Evaluator& eval);
};

struct InfosetKey {
  uint16_t hand_bucket;
  uint8_t street;
  uint16_t pot_size;
  uint8_t bet_sequence;
  uint8_t player;

  uint64_t hash() const;

  bool operator==(const InfosetKey& other) const { return hash() == other.hash(); }
};

inline uint16_t quantize_pot(double pot_ratio) {
  if (pot_ratio < 0) pot_ratio = 0;
  if (pot_ratio > 32) pot_ratio = 32;
  return static_cast<uint16_t>(pot_ratio);
}

struct BetHistory {
  static constexpr int kMaxRounds = 4;
  uint8_t rounds[kMaxRounds];
  uint8_t num_rounds = 0;

  void add_round_bet(uint8_t rel_bet) {
    if (num_rounds < kMaxRounds) rounds[num_rounds++] = rel_bet & 0x07;
  }

  uint16_t encode() const {
    uint16_t code = 0;
    for (int i = 0; i < num_rounds; ++i) {
      code |= (static_cast<uint16_t>(rounds[i]) << (i * 3));
    }
    return code;
  }
};

}  // namespace poker_engine::cfr

namespace std {
template <>
struct hash<poker_engine::cfr::InfosetKey> {
  size_t operator()(const poker_engine::cfr::InfosetKey& k) const { return k.hash(); }
};
}  // namespace std
