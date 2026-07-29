#pragma once
#include <cstdint>

#include "poker_engine/evaluator/hand.h"

namespace poker_engine {
namespace evaluator {

struct EvalResult {
  HandCategory category = HandCategory::HighCard;
  uint8_t rank[5] = {0, 0, 0, 0, 0};

  // Monotonic strength score: higher = stronger hand.
  // Encodes category in the highest bits, then rank[0..4] as kickers.
  uint32_t value() const {
    uint32_t v = static_cast<uint8_t>(category);
    v = (v << 4) | rank[0];
    v = (v << 4) | rank[1];
    v = (v << 4) | rank[2];
    v = (v << 4) | rank[3];
    v = (v << 4) | rank[4];
    return v;
  }

  // standard_rank for API compat (lower = better, Two Plus Two convention)
  uint16_t standard_rank = 7462;

  bool operator==(const EvalResult& o) const { return value() == o.value(); }
  bool operator!=(const EvalResult& o) const { return value() != o.value(); }
  bool operator<(const EvalResult& o) const { return value() < o.value(); }
  bool operator>(const EvalResult& o) const { return value() > o.value(); }
  bool operator<=(const EvalResult& o) const { return value() <= o.value(); }
  bool operator>=(const EvalResult& o) const { return value() >= o.value(); }
};

class Evaluator {
 public:
  static EvalResult Evaluate5(const Card ids[5]);
  static EvalResult Evaluate5(const FiveCards& hand);
  static EvalResult Evaluate7(const uint8_t ids[7]);
  static EvalResult Evaluate7(const Card ids[7]);
  static EvalResult Evaluate7(const SevenCards& hand);

  // CI gate: exhaustive C(52,5) check against phevaluator tables.
  static bool SelfCheck();
};

}  // namespace evaluator
}  // namespace poker_engine
