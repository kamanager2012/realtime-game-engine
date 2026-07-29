#include "poker_engine/evaluator/evaluator.h"

#include <algorithm>

namespace poker_engine {
namespace evaluator {
namespace {

// FIX: 替换为正确的 5 元素排序网络 (Bose-Nelson, 9 次比较)
void Sort5ByRank(const Card ids[5], Card sorted[5]) {
  for (int i = 0; i < 5; i++) sorted[i] = ids[i];
  auto cs = [](Card& a, Card& b) {
    if (a.RankIndex() > b.RankIndex()) std::swap(a, b);
  };
  cs(sorted[0], sorted[1]);  // step 1
  cs(sorted[3], sorted[4]);  // step 2
  cs(sorted[2], sorted[4]);  // step 3
  cs(sorted[2], sorted[3]);  // step 4
  cs(sorted[1], sorted[4]);  // step 5
  cs(sorted[0], sorted[3]);  // step 6
  cs(sorted[0], sorted[2]);  // step 7
  cs(sorted[1], sorted[3]);  // step 8
  cs(sorted[1], sorted[2]);  // step 9
}

bool IsFlush(const Card ids[5]) {
  Suit s = ids[0].GetSuit();
  return ids[1].GetSuit() == s && ids[2].GetSuit() == s && ids[3].GetSuit() == s &&
         ids[4].GetSuit() == s;
}

int CheckStraight(const uint8_t r[5], bool& wheel) {
  wheel = false;
  if (r[4] - r[3] == 1 && r[3] - r[2] == 1 && r[2] - r[1] == 1 && r[1] - r[0] == 1)
    return int(r[4]);
  if (r[0] == 0 && r[1] == 1 && r[2] == 2 && r[3] == 3 && r[4] == 12) {
    wheel = true;
    return 3;
  }
  return -1;
}

// Helpers for straight-flush detection over 7 cards.
bool CountStraightSuit(const uint8_t ranks[7], const uint8_t suits[7], int suit,
                        int rank) {
  for (int i = 0; i < 7; i++)
    if (ranks[i] == rank && suits[i] == suit) return true;
  return false;
}
bool StraightFlushHasWheel(const uint8_t ranks[7], const uint8_t suits[7], int suit) {
  for (int r : {12, 0, 1, 2, 3})
    if (!CountStraightSuit(ranks, suits, suit, r)) return false;
  return true;
}

EvalResult Evaluate5Sorted(const Card sorted[5]) {
  uint8_t ranks[5];
  for (int i = 0; i < 5; i++) ranks[i] = sorted[i].RankIndex();

  bool wheel = false;
  int sh = CheckStraight(ranks, wheel);
  bool straight = (sh >= 0);
  bool flush = IsFlush(sorted);

  if (flush && straight) return {HandCategory::StraightFlush, {uint8_t(sh), 0, 0, 0, 0}};

  int count[13] = {0};
  for (int i = 0; i < 5; i++) count[ranks[i]]++;

  int quad = -1, trips = -1, pairs[2] = {-1, -1}, np = 0;
  for (int r = 12; r >= 0; r--) {
    if (count[r] == 4)
      quad = r;
    else if (count[r] == 3)
      trips = r;
    else if (count[r] == 2 && np < 2)
      pairs[np++] = r;
  }

  if (quad >= 0) {
    int k = 12;
    while ((count[k] == 0 || k == quad) && k >= 0) k--;
    return {HandCategory::FourOfAKind, {uint8_t(quad), uint8_t(k), 0, 0, 0}};
  }
  if (trips >= 0 && np >= 1)
    return {HandCategory::FullHouse, {uint8_t(trips), uint8_t(pairs[0]), 0, 0, 0}};
  if (flush) return {HandCategory::Flush, {ranks[4], ranks[3], ranks[2], ranks[1], ranks[0]}};
  if (straight) return {HandCategory::Straight, {uint8_t(sh), 0, 0, 0, 0}};
  if (trips >= 0) {
    int k[2] = {-1, -1}, ki = 0;
    for (int r = 12; r >= 0 && ki < 2; r--)
      if (count[r] > 0 && r != trips) k[ki++] = r;
    return {HandCategory::ThreeOfAKind, {uint8_t(trips), uint8_t(k[0]), uint8_t(k[1]), 0, 0}};
  }
  if (np == 2) {
    int k = 12;
    while (count[k] == 0 || k == pairs[0] || k == pairs[1]) k--;
    return {HandCategory::TwoPair, {uint8_t(pairs[0]), uint8_t(pairs[1]), uint8_t(k), 0, 0}};
  }
  if (np == 1) {
    int k[3] = {-1, -1, -1}, ki = 0;
    for (int r = 12; r >= 0 && ki < 3; r--)
      if (count[r] > 0 && r != pairs[0]) k[ki++] = r;
    return {HandCategory::OnePair,
            {uint8_t(pairs[0]), uint8_t(k[0]), uint8_t(k[1]), uint8_t(k[2])}};
  }
  return {HandCategory::HighCard, {ranks[4], ranks[3], ranks[2], ranks[1], ranks[0]}};
}

}  // namespace
}  // namespace evaluator

poker_engine::evaluator::EvalResult poker_engine::evaluator::Evaluator::Evaluate5(
    const Card ids[5]) {
  Card sorted[5];
  Sort5ByRank(ids, sorted);
  return Evaluate5Sorted(sorted);
}

poker_engine::evaluator::EvalResult poker_engine::evaluator::Evaluator::Evaluate5(
    const FiveCards& hand) {
  Card ids[5];
  for (size_t i = 0; i < 5; i++) ids[i] = hand[i];
  return Evaluate5(ids);
}

// Fast 7-card evaluation over raw card ids (id = rank*4 + suit).
// Detects the best 5-card hand in a single pass instead of brute-forcing
// all 21 five-card combinations. Returns the strongest EvalResult.
poker_engine::evaluator::EvalResult poker_engine::evaluator::Evaluator::Evaluate7(
    const uint8_t ids[7]) {
  uint8_t ranks[7], suits[7];
  for (int i = 0; i < 7; i++) {
    ranks[i] = ids[i] >> 2;
    suits[i] = ids[i] & 3;
  }

  // Flush detection: any suit appearing >= 5 times.
  int suit_count[4] = {0, 0, 0, 0};
  for (int i = 0; i < 7; i++) suit_count[suits[i]]++;
  int flush_suit = -1;
  for (int s = 0; s < 4; s++)
    if (suit_count[s] >= 5) { flush_suit = s; break; }

  // Rank histogram + top-5 kickers.
  int count[13] = {0};
  for (int i = 0; i < 7; i++) count[ranks[i]]++;

  // Straight detection over the 7 distinct ranks (Ace high & wheel).
  int straight_high = -1;
  for (int hi = 12; hi >= 4; hi--) {
    if (count[hi] && count[hi - 1] && count[hi - 2] && count[hi - 3] && count[hi - 4]) {
      straight_high = hi;
      break;
    }
  }
  bool wheel = count[12] && count[0] && count[1] && count[2] && count[3];

  // Collect the (up to) 5 highest distinct ranks present, in descending order.
  uint8_t top5[5] = {0, 0, 0, 0, 0};
  int tk = 0;
  for (int r = 12; r >= 0 && tk < 5; r--)
    if (count[r]) top5[tk++] = uint8_t(r);

  auto flush_eval = [&]() -> EvalResult {
    // Build the 5 highest cards of the flush suit.
    uint8_t fr[5] = {0, 0, 0, 0, 0};
    int fk = 0;
    for (int r = 12; r >= 0 && fk < 5; r--)
      for (int i = 0; i < 7 && fk < 5; i++)
        if (ranks[i] == r && suits[i] == flush_suit) { fr[fk++] = uint8_t(r); break; }
    return {HandCategory::Flush, {fr[0], fr[1], fr[2], fr[3], fr[4]}};
  };

  if (flush_suit >= 0) {
    int sf_high = -1;
    bool sf_wheel = false;
    for (int hi = 12; hi >= 4; hi--) {
      bool ok = true;
        for (int k = 0; k < 5; k++)
        if (!CountStraightSuit(ranks, suits, flush_suit, hi - k)) { ok = false; break; }
      if (ok) { sf_high = hi; break; }
    }
    if (!sf_wheel) sf_wheel = count[12] && count[0] && count[1] && count[2] && count[3] &&
                                  StraightFlushHasWheel(ranks, suits, flush_suit);
    if (sf_high >= 0)
      return {HandCategory::StraightFlush, {uint8_t(sf_high), 0, 0, 0, 0}};
    if (sf_wheel)
      return {HandCategory::StraightFlush, {uint8_t(3), 0, 0, 0, 0}};
    return flush_eval();
  }

  if (straight_high >= 0)
    return {HandCategory::Straight, {uint8_t(straight_high), 0, 0, 0, 0}};
  if (wheel)
    return {HandCategory::Straight, {uint8_t(3), 0, 0, 0, 0}};

  // Collect rank groups by multiplicity (highest rank first).
  int quad = -1, trip[2] = {-1, -1}, tp = 0;
  int pair[3] = {-1, -1, -1}, np = 0;
  for (int r = 12; r >= 0; r--) {
    if (count[r] == 4)
      quad = r;
    else if (count[r] == 3) {
      if (tp < 2) trip[tp++] = r;
    } else if (count[r] == 2) {
      if (np < 3) pair[np++] = r;
    }
  }

  if (quad >= 0) {
    int k = 12;
    while ((count[k] == 0 || k == quad) && k >= 0) k--;
    return {HandCategory::FourOfAKind, {uint8_t(quad), uint8_t(k), 0, 0, 0}};
  }
  // Full house: a set plus (another set, or a pair).
  if (tp >= 1 && (tp >= 2 || np >= 1)) {
    int trio = trip[0];
    int duo = (tp >= 2) ? trip[1] : pair[0];
    return {HandCategory::FullHouse, {uint8_t(trio), uint8_t(duo), 0, 0, 0}};
  }
  if (tp >= 1) {
    int k[2] = {-1, -1}, ki = 0;
    for (int r = 12; r >= 0 && ki < 2; r--)
      if (count[r] > 0 && r != trip[0]) k[ki++] = r;
    return {HandCategory::ThreeOfAKind, {uint8_t(trip[0]), uint8_t(k[0]), uint8_t(k[1]), 0, 0}};
  }
  if (np >= 2) {
    int k = 12;
    while (count[k] == 0 || k == pair[0] || k == pair[1]) k--;
    return {HandCategory::TwoPair, {uint8_t(pair[0]), uint8_t(pair[1]), uint8_t(k), 0, 0}};
  }
  if (np == 1) {
    int k[3] = {-1, -1, -1}, ki = 0;
    for (int r = 12; r >= 0 && ki < 3; r--)
      if (count[r] > 0 && r != pair[0]) k[ki++] = r;
    return {HandCategory::OnePair,
            {uint8_t(pair[0]), uint8_t(k[0]), uint8_t(k[1]), uint8_t(k[2])}};
  }
  return {HandCategory::HighCard, {top5[0], top5[1], top5[2], top5[3], top5[4]}};
}

poker_engine::evaluator::EvalResult poker_engine::evaluator::Evaluator::Evaluate7(
    const Card ids[7]) {
  uint8_t raw[7];
  for (int i = 0; i < 7; i++) raw[i] = ids[i].Id();
  return Evaluate7(raw);
}

poker_engine::evaluator::EvalResult poker_engine::evaluator::Evaluator::Evaluate7(
    const SevenCards& hand) {
  uint8_t raw[7];
  for (size_t i = 0; i < 7; i++) raw[i] = hand[i].Id();
  return Evaluate7(raw);
}

}  // namespace poker_engine
