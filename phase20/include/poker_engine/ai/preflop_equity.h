#pragma once
#include <array>
#include <cstdint>

namespace poker_engine::ai {

// Precomputed preflop equity table: 169x169 heads-up matchup data.
// Generated offline by scripts/gen_preflop_equity.py using Monte Carlo.
// See preflop_equity_data.h for the actual values.

constexpr int kPreflopIndexCount = 169;

// Convert two hole cards to a 169-index.
// Returns the index (0-168), or -1 on invalid input.
int HandTo169Index(const std::array<uint8_t, 2>& hole_cards);

// Import the generated data table.
#include "poker_engine/ai/preflop_equity_data.h"

// Lookup heads-up equity. O(1), no allocation.
inline double LookupPreflopEquity(int hero_idx, int villain_idx) {
  if (hero_idx < 0 || hero_idx >= kPreflopIndexCount ||
      villain_idx < 0 || villain_idx >= kPreflopIndexCount)
    return 0.5;
  return kPreflopEquity[hero_idx][villain_idx];
}

}  // namespace poker_engine::ai
