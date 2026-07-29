#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

#include "poker_engine/base/types.h"

namespace poker_engine {
namespace range {

class SuitIsomorphism {
 public:
  static constexpr int ABSTRACT_HANDS = 169;
  static constexpr int SUITED_OFFSET = 13;
  static constexpr int OFFSUIT_OFFSET = 91;

  static AbstractId ToAbstract(uint8_t rank1, uint8_t suit1, uint8_t rank2, uint8_t suit2) {
    if (rank1 < rank2) {
      std::swap(rank1, rank2);
      std::swap(suit1, suit2);
    }
    uint16_t rp = rank1 * (rank1 - 1) / 2 + rank2;
    if (rank1 == rank2) return AbstractId(rank1);
    if (suit1 == suit2) return AbstractId(SUITED_OFFSET + rp);
    return AbstractId(OFFSUIT_OFFSET + rp);
  }

  static AbstractId ToAbstract(uint8_t c1, uint8_t c2) {
    return ToAbstract(c1 >> 2, c1 & 3, c2 >> 2, c2 & 3);
  }

  static void ToConcrete(AbstractId abs_id, uint8_t& c1, uint8_t& c2) {
    if (abs_id < 13) {
      c1 = abs_id * 4;
      c2 = c1 + 1;
    } else if (abs_id < 91) {
      uint16_t rp = abs_id - SUITED_OFFSET;
      DecodeRP(rp, c1, c2);
      c1 = c1 * 4;
      c2 = c2 * 4;
    } else {
      uint16_t rp = abs_id - OFFSUIT_OFFSET;
      DecodeRP(rp, c1, c2);
      c1 = c1 * 4;
      c2 = c2 * 4 + 1;
    }
  }

  static std::string AbstractName(AbstractId abs_id) {
    uint8_t c1, c2;
    ToConcrete(abs_id, c1, c2);
    uint8_t r1 = c1 >> 2, r2 = c2 >> 2;
    std::string n;
    n += RankChar(Rank(r1));
    n += RankChar(Rank(r2));
    if (r1 != r2) {
      if (abs_id < OFFSUIT_OFFSET)
        n += 's';
      else
        n += 'o';
    }
    return n;
  }

 private:
  static void DecodeRP(uint16_t idx, uint8_t& r1, uint8_t& r2) {
    r1 = 1;
    while (uint16_t((r1 + 1) * r1 / 2) <= idx) r1++;
    r2 = idx - r1 * (r1 - 1) / 2;
  }
};

}  // namespace range
}  // namespace poker_engine
