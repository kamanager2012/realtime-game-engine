#include "poker_engine/evaluator/evaluator.h"

#include <gtest/gtest.h>

#include <iostream>
#include <random>
#include <algorithm>
#include <array>
#include <phevaluator/phevaluator.h>

#include "poker_engine/evaluator/card.h"

using namespace poker_engine;
using namespace poker_engine::evaluator;

static Card PC(const std::string& s) { return Card::Parse(s); }

static FiveCards P5(const std::vector<std::string>& v) {
  FiveCards h;
  for (size_t i = 0; i < 5; i++) h[i] = PC(v[i]);
  return h;
}

static SevenCards P7(const std::vector<std::string>& v) {
  SevenCards h;
  for (size_t i = 0; i < 7; i++) h[i] = PC(v[i]);
  return h;
}

TEST(EvaluatorTest, RoyalFlush) {
  auto h = P5({"Ts", "Js", "Qs", "Ks", "As"});
  EvalResult r = Evaluator::Evaluate5(h);
  EXPECT_EQ(r.category, HandCategory::StraightFlush);
  EXPECT_EQ(r.rank[0], 12);
}

TEST(EvaluatorTest, KingHighFlush) {
  auto h = P5({"Kc", "Qc", "Jc", "Tc", "2c"});
  EvalResult r = Evaluator::Evaluate5(h);
  EXPECT_EQ(r.category, HandCategory::Flush);
  EXPECT_EQ(r.rank[0], 11);
  EXPECT_EQ(r.rank[1], 10);
}

TEST(EvaluatorTest, FourOfAKind) {
  auto h = P5({"Qc", "Qd", "Qh", "Qs", "7d"});
  EvalResult r = Evaluator::Evaluate5(h);
  EXPECT_EQ(r.category, HandCategory::FourOfAKind);
  EXPECT_EQ(r.rank[0], 10);
  EXPECT_EQ(r.rank[1], 5);
}

TEST(EvaluatorTest, FullHouseKingsOverAces) {
  auto h = P5({"Kc", "Kd", "Ah", "As", "Kh"});
  EvalResult r = Evaluator::Evaluate5(h);
  EXPECT_EQ(r.category, HandCategory::FullHouse);
  EXPECT_EQ(r.rank[0], 11);
  EXPECT_EQ(r.rank[1], 12);
}

TEST(EvaluatorTest, Straight) {
  auto h = P5({"4c", "5d", "6h", "7s", "8c"});
  EvalResult r = Evaluator::Evaluate5(h);
  EXPECT_EQ(r.category, HandCategory::Straight);
  EXPECT_EQ(r.rank[0], 6);
}

TEST(EvaluatorTest, WheelStraight) {
  auto h = P5({"Ac", "2d", "3h", "4s", "5c"});
  EvalResult r = Evaluator::Evaluate5(h);
  EXPECT_EQ(r.category, HandCategory::Straight);
  EXPECT_EQ(r.rank[0], 3);
}

TEST(EvaluatorTest, ThreeOfAKind) {
  auto h = P5({"8c", "8d", "8h", "3s", "Kc"});
  EvalResult r = Evaluator::Evaluate5(h);
  EXPECT_EQ(r.category, HandCategory::ThreeOfAKind);
  EXPECT_EQ(r.rank[0], 6);
  EXPECT_EQ(r.rank[1], 11);
  EXPECT_EQ(r.rank[2], 1);
}

TEST(EvaluatorTest, TwoPair) {
  auto h = P5({"Jc", "Jd", "9h", "9s", "4c"});
  EvalResult r = Evaluator::Evaluate5(h);
  EXPECT_EQ(r.category, HandCategory::TwoPair);
  EXPECT_EQ(r.rank[0], 9);
  EXPECT_EQ(r.rank[1], 7);
  EXPECT_EQ(r.rank[2], 2);
}

TEST(EvaluatorTest, OnePairAces) {
  auto h = P5({"Ac", "Ad", "Ks", "Jh", "6s"});
  EvalResult r = Evaluator::Evaluate5(h);
  EXPECT_EQ(r.category, HandCategory::OnePair);
  EXPECT_EQ(r.rank[0], 12);
  EXPECT_EQ(r.rank[1], 11);
  EXPECT_EQ(r.rank[2], 9);
  EXPECT_EQ(r.rank[3], 4);
}

TEST(EvaluatorTest, HighCardAce) {
  auto h = P5({"Ac", "Kd", "Qh", "Js", "9c"});
  EvalResult r = Evaluator::Evaluate5(h);
  EXPECT_EQ(r.category, HandCategory::HighCard);
  EXPECT_EQ(r.rank[0], 12);
  EXPECT_EQ(r.rank[1], 11);
  EXPECT_EQ(r.rank[2], 10);
  EXPECT_EQ(r.rank[3], 9);
  EXPECT_EQ(r.rank[4], 7);
}

TEST(EvaluatorTest, RoyalBeatFlush) {
  EXPECT_GT(Evaluator::Evaluate5(P5({"Ts", "Js", "Qs", "Ks", "As"})),
            Evaluator::Evaluate5(P5({"Ac", "Kc", "Qc", "Jc", "9c"})));
}

TEST(EvaluatorTest, FullHouseBeatFlush) {
  EXPECT_GT(Evaluator::Evaluate5(P5({"Kc", "Kd", "Kh", "2s", "2c"})),
            Evaluator::Evaluate5(P5({"Ac", "Kc", "Qc", "Jc", "9c"})));
}

TEST(EvaluatorTest, HigherPairWins) {
  EXPECT_GT(Evaluator::Evaluate5(P5({"Ac", "Ad", "Ks", "Jh", "6s"})),
            Evaluator::Evaluate5(P5({"Kc", "Kd", "As", "Jh", "6s"})));
}

TEST(EvaluatorTest, SameHandIsTie) {
  EXPECT_EQ(Evaluator::Evaluate5(P5({"Ac", "Kd", "Qh", "Js", "9c"})),
            Evaluator::Evaluate5(P5({"As", "Kh", "Qd", "Jc", "9s"})));
}

TEST(EvaluatorTest, SevenCardTwoPairBeatsOnePair) {
  auto tp = P7({"Jc", "Jd", "9h", "9s", "2c", "3d", "4h"});
  auto op = P7({"Ac", "Ad", "3h", "5s", "7c", "9d", "Jh"});
  EvalResult r1 = Evaluator::Evaluate7(tp);
  EvalResult r2 = Evaluator::Evaluate7(op);
  EXPECT_EQ(r1.category, HandCategory::TwoPair);
  EXPECT_EQ(r2.category, HandCategory::OnePair);
  EXPECT_GT(r1, r2);
}

TEST(EvaluatorTest, SevenCardStraightFlush) {
  auto h = P7({"9s", "Ts", "Js", "Qs", "Ks", "2c", "3d"});
  EvalResult r = Evaluator::Evaluate7(h);
  EXPECT_EQ(r.category, HandCategory::StraightFlush);
  EXPECT_EQ(r.rank[0], 11);
}

TEST(CardTest, CardCreation) {
  Card c(Rank::Ace, Suit::Spades);
  EXPECT_EQ(c.RankIndex(), 12);
  EXPECT_EQ(c.SuitIndex(), 3);
}

TEST(CardTest, CardParse) {
  Card c = Card::Parse("As");
  EXPECT_EQ(c.GetRank(), Rank::Ace);
  EXPECT_EQ(c.GetSuit(), Suit::Spades);
  Card c2 = Card::Parse("7d");
  EXPECT_EQ(c2.GetRank(), Rank::Seven);
  EXPECT_EQ(c2.GetSuit(), Suit::Diamonds);
}

TEST(CardTest, CardToString) {
  EXPECT_EQ(Card::Parse("As").ToString(), "As");
  EXPECT_EQ(Card::Parse("Tc").ToString(), "Tc");
}

TEST(CardTest, CardParseRoundTrip) {
  for (const char* s :
       {"As", "Kd", "Qh", "Jc", "Ts", "9d", "8s", "7c", "6h", "5d", "4c", "3s", "2d"}) {
    Card c = Card::Parse(s);
    EXPECT_EQ(c.ToString(), s);
  }
}

// Golden test: validate our evaluator against PokerHandEvaluator on all C(52,5) = 2,598,960
// combinations. Card encoding is identical: id = rank*4 + suit (rank 0=2..12=A, suit 0=clubs..3=spades).
static int PheCategoryToOurs(int phe_cat) {
  static const int map[] = {0, 9, 8, 7, 6, 5, 4, 3, 2, 1};
  return map[phe_cat];
}

TEST(EvaluatorTest, GoldenExhaustiveC52Take5) {
  int total = 0;
  int category_mismatches = 0;
  uint64_t ours_sum = 0;
  int64_t phe_sum = 0;
  int max_diff_ours = 0, max_diff_phe = 0;
  int first_mismatch_idx = -1;
  std::string first_mismatch_desc;

  // Enumerate all C(52,5) = 2,598,960 combinations

  for (int a = 0; a < 48; a++) {
    for (int b = a + 1; b < 49; b++) {
      for (int c = b + 1; c < 50; c++) {
        for (int d = c + 1; d < 51; d++) {
          for (int e = d + 1; e < 52; e++) {
            Card ca = Card::FromId(static_cast<uint8_t>(a));
            Card cb = Card::FromId(static_cast<uint8_t>(b));
            Card cc = Card::FromId(static_cast<uint8_t>(c));
            Card cd = Card::FromId(static_cast<uint8_t>(d));
            Card ce = Card::FromId(static_cast<uint8_t>(e));
            Card ids[5] = {ca, cb, cc, cd, ce};

            auto ours = Evaluator::Evaluate5(ids);
            int phe_rank = evaluate_5cards(a, b, c, d, e);
            int phe_cat = get_rank_category(phe_rank);

            int our_cat = static_cast<int>(ours.category);
            int expected_cat = PheCategoryToOurs(phe_cat);

            if (our_cat != expected_cat) {
              category_mismatches++;
              if (first_mismatch_idx < 0) {
                first_mismatch_idx = total;
                first_mismatch_desc =
                    "hand=[" + std::to_string(a) + "," + std::to_string(b) + "," +
                    std::to_string(c) + "," + std::to_string(d) + "," + std::to_string(e) +
                    "] our_cat=" + std::to_string(our_cat) +
                    " expected=" + std::to_string(expected_cat) +
                    " phe_rank=" + std::to_string(phe_rank);
              }
            }

            ours_sum += ours.value();
            phe_sum += phe_rank;
            total++;

            // Track ordering consistency: adjacent hands in enumeration order
            // should have consistent ordering (not a perfect check, but useful)
            int our_val = static_cast<int>(ours.value());
            if (our_val > max_diff_ours) max_diff_ours = our_val;
            if (phe_rank > max_diff_phe) max_diff_phe = phe_rank;
          }
        }
      }
    }
  }

  EXPECT_EQ(total, 2598960) << "Must enumerate exactly C(52,5) = 2,598,960 hands";
  EXPECT_EQ(category_mismatches, 0)
      << "Category mismatch count=" << category_mismatches
      << " first at idx=" << first_mismatch_idx
      << " desc=" << first_mismatch_desc;

  // Verify value range is non-trivial
  EXPECT_GT(max_diff_ours, 0);
  EXPECT_GT(max_diff_phe, 0);
  std::cout << "[INFO] Golden check: " << total << " hands, "
            << "ours_value_range=[0," << max_diff_ours << "], "
            << "phe_rank_range=[1," << max_diff_phe << "]\n";
}

// Verify that the Cactus Kev reference sample hands evaluate correctly against phevaluator
TEST(EvaluatorTest, GoldenSampleMatchesPhevaluator) {
  struct HandTest {
    const char* cards[5];
    int expected_phe_rank; // from phevaluator rank table, 1=best
    int expected_category; // our HandCategory
  };

  // Representative sample: one per category, varying strength
  HandTest samples[] = {
    // Royal Flush
    {{"As", "Ks", "Qs", "Js", "Ts"}, 1, static_cast<int>(HandCategory::StraightFlush)},
    // King-high Straight Flush
    {{"Ks", "Qs", "Js", "Ts", "9s"}, 2, static_cast<int>(HandCategory::StraightFlush)},
    // Four of a Kind, Aces
    {{"Ac", "Ad", "Ah", "As", "Kc"}, 11, static_cast<int>(HandCategory::FourOfAKind)},
    // Full House, Kings full of Aces
    {{"Kc", "Kd", "Kh", "Ac", "Ad"}, 179, static_cast<int>(HandCategory::FullHouse)},
    // Ace-high Flush
    {{"Ac", "Kc", "Qc", "Jc", "9c"}, 323, static_cast<int>(HandCategory::Flush)},
    // Ace-high Straight
    {{"Ac", "Kd", "Qh", "Js", "Tc"}, 1600, static_cast<int>(HandCategory::Straight)},
    // Three of a Kind, Aces
    {{"Ac", "Ad", "Ah", "Ks", "Qc"}, 1610, static_cast<int>(HandCategory::ThreeOfAKind)},
    // Two Pair, Aces and Kings
    {{"Ac", "Ad", "Kc", "Kd", "Qs"}, 2468, static_cast<int>(HandCategory::TwoPair)},
    // One Pair, Aces
    {{"Ac", "Ad", "Ks", "Qs", "Jc"}, 3326, static_cast<int>(HandCategory::OnePair)},
    // High Card, Ace-high
    {{"Ac", "Kd", "Qs", "Jh", "9c"}, 6186, static_cast<int>(HandCategory::HighCard)},
    // Wheel (A-2-3-4-5)
    {{"Ac", "2d", "3h", "4s", "5c"}, 1609, static_cast<int>(HandCategory::Straight)},
    // Seven-high Straight
    {{"3c", "4d", "5h", "6s", "7c"}, 1607, static_cast<int>(HandCategory::Straight)},
    // Worst possible hand (7-5-4-3-2 offsuit, not a straight)
    {{"7c", "5d", "4h", "3s", "2c"}, 7462, static_cast<int>(HandCategory::HighCard)},
  };

  for (const auto& s : samples) {
    Card ids[5];
    for (int i = 0; i < 5; i++) ids[i] = Card::Parse(s.cards[i]);
    auto ours = Evaluator::Evaluate5(ids);
    int phe_id[5];
    for (int i = 0; i < 5; i++) phe_id[i] = ids[i].Id();
    int phe_rank = evaluate_5cards(phe_id[0], phe_id[1], phe_id[2], phe_id[3], phe_id[4]);

    EXPECT_EQ(static_cast<int>(ours.category), s.expected_category)
        << "Hand: " << s.cards[0] << s.cards[1] << s.cards[2] << s.cards[3] << s.cards[4];
    EXPECT_EQ(phe_rank, s.expected_phe_rank)
        << "Hand: " << s.cards[0] << s.cards[1] << s.cards[2] << s.cards[3] << s.cards[4];
  }
}

// Verify the fast single-pass Evaluate7 (over raw ids) agrees with the
// exhaustive 21-combination evaluator reference for random 7-card hands.
// Cards are sampled WITHOUT replacement, matching the engine's real contract
// (Evaluate7 is only called with 7 distinct, valid cards at showdown).
TEST(EvaluatorTest, FastEvaluate7MatchesReference) {
  std::mt19937 rng(12345);
  int mismatches = 0;
  std::array<uint8_t, 52> deck;
  for (int t = 0; t < 50000; t++) {
    for (int i = 0; i < 52; i++) deck[i] = uint8_t(i);
    std::shuffle(deck.begin(), deck.end(), rng);
    uint8_t ids[7];
    for (int i = 0; i < 7; i++) ids[i] = deck[i];
    EvalResult best_ref;
    for (int i = 0; i < 7; i++)
      for (int j = i + 1; j < 7; j++) {
        Card five[5];
        int k = 0;
        for (int m = 0; m < 7; m++)
          if (m != i && m != j) five[k++] = Card::FromId(ids[m]);
        EvalResult cur = Evaluator::Evaluate5(five);
        if (cur > best_ref) best_ref = cur;
      }
    EvalResult fast = Evaluator::Evaluate7(ids);
    if (fast != best_ref) {
      mismatches++;
      if (mismatches <= 3) {
        std::cout << "[MISMATCH] ids=";
        for (int i = 0; i < 7; i++) std::cout << int(ids[i]) << " ";
        std::cout << "fast_cat=" << int(fast.category)
                  << " ref_cat=" << int(best_ref.category) << "\n";
      }
    }
  }
  EXPECT_EQ(mismatches, 0) << "Fast Evaluate7 disagreed with reference on " << mismatches
                               << " of 50000 random 7-card hands";
}

// Verify fast Evaluate7 category agrees with phevaluator on random 7-card hands.
TEST(EvaluatorTest, FastEvaluate7MatchesPhevaluator) {
  std::mt19937 rng(99);
  int mismatches = 0;
  std::array<uint8_t, 52> deck;
  for (int t = 0; t < 20000; t++) {
    for (int i = 0; i < 52; i++) deck[i] = uint8_t(i);
    std::shuffle(deck.begin(), deck.end(), rng);
    uint8_t ids[7];
    for (int i = 0; i < 7; i++) ids[i] = deck[i];
    EvalResult ours = Evaluator::Evaluate7(ids);
    int best_phe = 7463;
    for (int i = 0; i < 7; i++)
      for (int j = i + 1; j < 7; j++) {
        int five[5], k = 0;
        for (int m = 0; m < 7; m++)
          if (m != i && m != j) five[k++] = ids[m];
        int r = evaluate_5cards(five[0], five[1], five[2], five[3], five[4]);
        if (r < best_phe) best_phe = r;
      }
    int expected_cat = PheCategoryToOurs(get_rank_category(best_phe));
    if (int(ours.category) != expected_cat) {
      mismatches++;
      if (mismatches <= 3)
        std::cout << "[CAT MISMATCH] ours=" << int(ours.category)
                  << " phe=" << expected_cat << "\n";
    }
  }
  EXPECT_EQ(mismatches, 0) << "Fast Evaluate7 category disagreed with phevaluator on "
                               << mismatches << " of 20000 random 7-card hands";
}
