#include "poker_engine/evaluator/hand_evaluator.h"

#include <algorithm>
#include <array>
#include <random>

namespace poker_engine::evaluator {

namespace {

inline int CardRank(uint8_t c) { return c % 13; }
inline int CardSuit(uint8_t c) { return c / 13; }

struct Card5 {
  uint8_t rank : 4;
  uint8_t suit : 4;
};

static_assert(sizeof(Card5) == 1, "Card5 should be 1 byte");

uint16_t QuickEval5(Card5 c[5]) {
  // Sort descending: Ace(0) -> sort-rank 13 (highest)
  auto sr = [](uint8_t r) -> int { return r == 0 ? 13 : r; };
  for (int i = 0; i < 4; ++i)
    for (int j = i + 1; j < 5; ++j)
      if (sr(c[i].rank) < sr(c[j].rank)) std::swap(c[i], c[j]);

  int r0 = sr(c[0].rank), r1 = sr(c[1].rank), r2 = sr(c[2].rank);
  int r3 = sr(c[3].rank), r4 = sr(c[4].rank);

  bool flush = (c[0].suit == c[1].suit && c[1].suit == c[2].suit && c[2].suit == c[3].suit &&
                c[3].suit == c[4].suit);

  bool straight = (r0 == r1 + 1 && r1 == r2 + 1 && r2 == r3 + 1 && r3 == r4 + 1);
  bool wheel = false;
  if (r0 == 13 && r1 == 4 && r2 == 3 && r3 == 2 && r4 == 1) {
    straight = true;
    wheel = true;
  }

  // Return: type * 3000 + tb  (max tb = 13*14*14+13*14+13 = 2743 < 10000)
  int type, tb;

  if (flush && straight && !wheel && r0 == 13) {
    type = 9;
    tb = 0;
  } else if (flush && straight) {
    type = 8;
    tb = wheel ? 1 : r0;
  } else if (r0 == r3 || r1 == r4) {
    type = 7;
    tb = (r0 == r3) ? r0 * 14 + r4 : r1 * 14 + r0;
  } else if ((r0 == r1 && r2 == r4) || (r0 == r2 && r3 == r4)) {
    type = 6;
    tb = (r0 == r1) ? r0 * 14 + r2 : r0 * 14 + r3;
  } else if (flush) {
    type = 5;
    tb = r0 * 14 * 14 + r1 * 14 + r2;
  } else if (straight) {
    type = 4;
    tb = wheel ? 1 : r0;
  } else if (r0 == r2 || r1 == r3 || r2 == r4) {
    type = 3;
    tb = (r0 == r2) ? r0 : (r1 == r3) ? r1 : r2;
  } else if ((r0 == r1 && r2 == r3) || (r0 == r1 && r3 == r4) || (r1 == r2 && r3 == r4)) {
    type = 2;
    if (r0 == r1 && r2 == r3)
      tb = r0 * 14 + r2;
    else if (r0 == r1 && r3 == r4)
      tb = r0 * 14 + r3;
    else
      tb = r1 * 14 + r3;
  } else if (r0 == r1 || r1 == r2 || r2 == r3 || r3 == r4) {
    type = 1;
    tb = (r0 == r1) ? r0 : (r1 == r2) ? r1 : (r2 == r3) ? r2 : r3;
  } else {
    type = 0;
    tb = r0 * 14 * 14 + r1 * 14 + r2;
  }

  return static_cast<uint16_t>(type * 3000 + tb);
}

}  // namespace

HandEvaluator::HandEvaluator() { PrecomputeTwoCardRanks(); }

void HandEvaluator::PrecomputeTwoCardRanks() {
  two_card_rank_.fill(0xFFFF);

  for (int c1 = 0; c1 < 52; ++c1) {
    for (int c2 = c1 + 1; c2 < 52; ++c2) {
      if (CardRank(c1) == CardRank(c2)) continue;

      int r1 = CardRank(c1), r2 = CardRank(c2);
      bool suited = (CardSuit(c1) == CardSuit(c2));

      uint16_t bucket = static_cast<uint16_t>(std::min(r1, r2) * 13 + std::max(r1, r2));
      if (suited) bucket += 13 * 13;

      two_card_rank_[c1 * 52 + c2] = bucket;
      two_card_rank_[c2 * 52 + c1] = bucket;
    }
  }

  for (int r = 0; r < 13; ++r) {
    for (int s1 = 0; s1 < 4; ++s1) {
      for (int s2 = s1 + 1; s2 < 4; ++s2) {
        int c1 = r + s1 * 13;
        int c2 = r + s2 * 13;
        two_card_rank_[c1 * 52 + c2] = 169 + static_cast<uint16_t>(r);
        two_card_rank_[c2 * 52 + c1] = 169 + static_cast<uint16_t>(r);
      }
    }
  }
}

HandResult HandEvaluator::Evaluate(const uint8_t cards[], int num_cards) const {
  HandResult result;
  result.rank = HandRank::HighCard;
  result.tiebreaker = 0;
  result.strength = 0;

  if (num_cards < 5) {
    result.strength = -1;
    return result;
  }

  if (num_cards == 5) {
    Card5 c5[5];
    for (int i = 0; i < 5; ++i) {
      c5[i].rank = CardRank(cards[i]);
      c5[i].suit = CardSuit(cards[i]);
    }

    uint16_t eval = QuickEval5(c5);

    int type = eval / 3000;  // hand type 0..9

    switch (type) {
      case 9:
        result.rank = HandRank::RoyalFlush;
        break;
      case 8:
        result.rank = HandRank::StraightFlush;
        break;
      case 7:
        result.rank = HandRank::FourOfKind;
        break;
      case 6:
        result.rank = HandRank::FullHouse;
        break;
      case 5:
        result.rank = HandRank::Flush;
        break;
      case 4:
        result.rank = HandRank::Straight;
        break;
      case 3:
        result.rank = HandRank::ThreeOfKind;
        break;
      case 2:
        result.rank = HandRank::TwoPair;
        break;
      case 1:
        result.rank = HandRank::OnePair;
        break;
      default:
        result.rank = HandRank::HighCard;
        break;
    }

    result.tiebreaker = eval;
    result.strength = eval;  // higher = better

  } else {
    result = EvaluateSevenCards(cards);
  }

  return result;
}

HandResult HandEvaluator::EvaluateSevenCards(const uint8_t cards[7]) const {
  HandResult best;
  best.strength = -1;

  for (int a = 0; a < 7; ++a) {
    for (int b = a + 1; b < 7; ++b) {
      for (int c = b + 1; c < 7; ++c) {
        for (int d = c + 1; d < 7; ++d) {
          for (int e = d + 1; e < 7; ++e) {
            uint8_t five[5] = {cards[a], cards[b], cards[c], cards[d], cards[e]};
            HandResult r = Evaluate(five, 5);
            if (r.strength > best.strength) {
              best = r;
            }
          }
        }
      }
    }
  }

  return best;
}

int HandEvaluator::CompareTwoHands(uint8_t h1_c1, uint8_t h1_c2, uint8_t h2_c1, uint8_t h2_c2,
                                   const uint8_t community[5], int comm_count) const {
  uint8_t h1[7], h2[7];
  h1[0] = h1_c1;
  h1[1] = h1_c2;
  h2[0] = h2_c1;
  h2[1] = h2_c2;

  for (int i = 0; i < comm_count; ++i) {
    h1[2 + i] = community[i];
    h2[2 + i] = community[i];
  }

  HandResult r1 = Evaluate(h1, 2 + comm_count);
  HandResult r2 = Evaluate(h2, 2 + comm_count);

  if (r1.strength > r2.strength) return 1;
  if (r1.strength < r2.strength) return -1;
  return 0;
}

double HandEvaluator::GetEquity(uint8_t c1, uint8_t c2, const uint8_t community[5], int comm_count,
                                int num_opponents) const {
  int wins = 0, ties = 0, total = 0;
  std::array<bool, 52> used{};
  used.fill(false);

  for (int i = 0; i < comm_count; ++i) used[community[i]] = true;
  used[c1] = used[c2] = true;

  std::vector<int> available;
  for (int i = 0; i < 52; ++i) {
    if (!used[i]) available.push_back(i);
  }

  std::mt19937 rng(42);
  int needed = 5 - comm_count;
  int opp_offset = needed;  // opponent cards start after community fillers

  for (int sim = 0; sim < 2000; ++sim) {
    std::shuffle(available.begin(), available.end(), rng);

    // Build full 7-card hands
    uint8_t my_hand[7], opp_hand[7];
    my_hand[0] = c1;
    my_hand[1] = c2;

    // Opponent hole cards
    uint8_t opp_c1 = static_cast<uint8_t>(available[0]);
    uint8_t opp_c2 = static_cast<uint8_t>(available[1]);
    opp_hand[0] = opp_c1;
    opp_hand[1] = opp_c2;

    // Community cards (known + simulated)
    uint8_t full_community[5];
    for (int i = 0; i < comm_count; ++i) {
      full_community[i] = community[i];
    }
    for (int i = 0; i < needed; ++i) {
      full_community[comm_count + i] = static_cast<uint8_t>(available[2 + i]);
    }

    for (int i = 0; i < 5; ++i) {
      my_hand[2 + i] = full_community[i];
      opp_hand[2 + i] = full_community[i];
    }

    HandResult r1 = Evaluate(my_hand, 7);
    HandResult r2 = Evaluate(opp_hand, 7);

    if (r1.strength > r2.strength)
      wins++;
    else if (r1.strength == r2.strength)
      ties++;
    total++;
  }

  return total > 0 ? static_cast<double>(wins + ties * 0.5) / total : 0.5;
}

uint16_t HandEvaluator::TwoCardHash(uint8_t c1, uint8_t c2) const {
  if (c1 > c2) std::swap(c1, c2);
  return two_card_rank_[c1 * 52 + c2];
}

const char* HandEvaluator::HandRankName(HandRank rank) {
  switch (rank) {
    case HandRank::HighCard:
      return "High Card";
    case HandRank::OnePair:
      return "One Pair";
    case HandRank::TwoPair:
      return "Two Pair";
    case HandRank::ThreeOfKind:
      return "Three of a Kind";
    case HandRank::Straight:
      return "Straight";
    case HandRank::Flush:
      return "Flush";
    case HandRank::FullHouse:
      return "Full House";
    case HandRank::FourOfKind:
      return "Four of a Kind";
    case HandRank::StraightFlush:
      return "Straight Flush";
    case HandRank::RoyalFlush:
      return "Royal Flush";
  }
  return "Unknown";
}

}  // namespace poker_engine::evaluator
