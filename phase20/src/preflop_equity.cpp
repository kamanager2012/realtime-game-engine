#include "poker_engine/ai/preflop_equity.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace poker_engine::ai {

namespace {

constexpr int kRankIndex[13] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

inline int CardRank(int card_id) { return card_id / 4; }
inline int CardSuit(int card_id) { return card_id % 4; }

int Compute169Index(int r1, int r2, bool suited) {
  // r1 >= r2 (higher rank first)
  int idx = 0;
  if (suited) {
    for (int i = 0; i < r1; ++i) idx += (12 - i);
    idx += r2;
    return 13 + idx;
  } else {
    for (int i = 0; i < r1; ++i) idx += (12 - i);
    idx += r2;
    return 91 + idx;
  }
}

}  // namespace

int HandTo169Index(const std::array<uint8_t, 2>& cards) {
  int r1 = CardRank(cards[0]);
  int r2 = CardRank(cards[1]);
  bool suited = (CardSuit(cards[0]) == CardSuit(cards[1]));
  if (r1 < r2) { std::swap(r1, r2); }
  return Compute169Index(r1, r2, suited);
}

}  // namespace poker_engine::ai
