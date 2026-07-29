#pragma once
#include <cstdint>
#include <cmath>
#include <utility>

#include "poker_engine/base/types.h"

namespace poker_engine {
namespace range {

class HandId {
 public:
  static constexpr int TOTAL_COMBOS = 1326;

  static uint16_t Encode(uint8_t c1, uint8_t c2) {
    uint8_t lo = c1 < c2 ? c1 : c2, hi = c1 < c2 ? c2 : c1;
    return uint16_t(hi * (hi - 1) / 2 + lo);
  }

    static std::pair<uint8_t, uint8_t> Decode(uint16_t id) {
    // Inverse of Encode: id = h*(h-1)/2 + lo, with 0 <= lo < h.
    // Closed form h = floor((1+sqrt(1+8*id))/2), corrected for FP rounding.
    uint16_t h = static_cast<uint16_t>(std::floor((1.0 + std::sqrt(1.0 + 8.0 * id)) / 2.0));
    while (uint16_t(h * (h - 1) / 2) > id) h--;
    while (uint16_t((h + 1) * h / 2) <= id) h++;
    uint8_t lo = static_cast<uint8_t>(id - h * (h - 1) / 2);
    return {lo, static_cast<uint8_t>(h)};
  }
};

}  // namespace range
}  // namespace poker_engine
