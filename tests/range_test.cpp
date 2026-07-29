#include "poker_engine/range/range.h"

#include <gtest/gtest.h>

#include <set>

#include "poker_engine/range/hand_id.h"
#include "poker_engine/range/suit_iso.h"

using namespace poker_engine;
using namespace poker_engine::range;
using poker_engine::AbstractId;

TEST(HandIdTest, EncodeDecode) {
  for (uint8_t c1 = 0; c1 < 52; c1++)
    for (uint8_t c2 = c1 + 1; c2 < 52; c2++) {
      auto id = HandId::Encode(c1, c2);
      auto [dl, dh] = HandId::Decode(id);
      EXPECT_EQ(dl, c1);
      EXPECT_EQ(dh, c2);
    }
}

TEST(HandIdTest, TotalCombos) {
  std::set<uint16_t> ids;
  for (uint8_t c1 = 0; c1 < 52; c1++)
    for (uint8_t c2 = c1 + 1; c2 < 52; c2++) ids.insert(HandId::Encode(c1, c2));
  EXPECT_EQ(ids.size(), 1326);
}

TEST(SuitIsoTest, PocketPairAbstraction) {
  EXPECT_EQ(SuitIsomorphism::ToAbstract(48, 51), SuitIsomorphism::ToAbstract(49, 50));
  EXPECT_LT(SuitIsomorphism::ToAbstract(48, 51), 13);
}

TEST(SuitIsoTest, SuitedHands) {
  EXPECT_EQ(SuitIsomorphism::ToAbstract(48, 20), SuitIsomorphism::ToAbstract(49, 21));
  EXPECT_LT(SuitIsomorphism::ToAbstract(48, 20), SuitIsomorphism::OFFSUIT_OFFSET);
}

TEST(SuitIsoTest, OffsuitHands) {
  EXPECT_EQ(SuitIsomorphism::ToAbstract(48, 22), SuitIsomorphism::ToAbstract(49, 20));
  EXPECT_GE(SuitIsomorphism::ToAbstract(48, 22), SuitIsomorphism::OFFSUIT_OFFSET);
}

TEST(SuitIsoTest, RoundTrip) {
  for (AbstractId id = 0; id < SuitIsomorphism::ABSTRACT_HANDS; id++) {
    uint8_t c1, c2;
    SuitIsomorphism::ToConcrete(id, c1, c2);
    EXPECT_EQ(SuitIsomorphism::ToAbstract(c1, c2), id);
  }
}

TEST(SuitIsoTest, AbstractNames) {
  EXPECT_EQ(SuitIsomorphism::AbstractName(12), "AA");
  EXPECT_EQ(SuitIsomorphism::AbstractName(0), "22");
  EXPECT_EQ(SuitIsomorphism::AbstractName(SuitIsomorphism::ToAbstract(51, 47)), "AKs");
  EXPECT_EQ(SuitIsomorphism::AbstractName(SuitIsomorphism::ToAbstract(51, 44)), "AKo");
}

TEST(RangeTest, EmptyRange) {
  Range r;
  EXPECT_EQ(r.Sum(), 0.0f);
  EXPECT_EQ(r.NonZeroCount(), 0);
}

TEST(RangeTest, FullRange) {
  Range r = Range::FullCombinatorial();
  EXPECT_EQ(r.NonZeroCount(), 1326);
  EXPECT_FLOAT_EQ(r.Sum(), 1326.0f);
}

TEST(RangeTest, PocketPairRange) {
  Range r = Range::FromString("AA");
  EXPECT_EQ(r.NonZeroCount(), 6);
  EXPECT_FLOAT_EQ(r.Sum(), 6.0f);
}

TEST(RangeTest, SuitedRange) {
  Range r = Range::FromString("AKs");
  EXPECT_EQ(r.NonZeroCount(), 4);
  EXPECT_FLOAT_EQ(r.Sum(), 4.0f);
}

TEST(RangeTest, OffsuitRange) {
  Range r = Range::FromString("AKo");
  EXPECT_EQ(r.NonZeroCount(), 12);
  EXPECT_FLOAT_EQ(r.Sum(), 12.0f);
}

TEST(RangeTest, AKAnySuit) {
  Range r = Range::FromString("AK");
  EXPECT_EQ(r.NonZeroCount(), 16);
  EXPECT_FLOAT_EQ(r.Sum(), 16.0f);
}

TEST(RangeTest, PocketPairPlus) { EXPECT_EQ(Range::FromString("QQ+").NonZeroCount(), 18); }

TEST(RangeTest, AllPairs) { EXPECT_EQ(Range::FromString("22+").NonZeroCount(), 78); }

TEST(RangeTest, SuitedBroadways) { EXPECT_EQ(Range::FromString("AKs-AJs").NonZeroCount(), 12); }

TEST(RangeTest, Normalize) {
  Range r = Range::FromString("AA,KK,QQ");
  r.Normalize();
  EXPECT_NEAR(r.Sum(), 1.0f, 0.0001f);
}

TEST(RangeTest, CardRemoval) {
  Range r = Range::FromString("22+");
  r.RemoveCard(48);
  EXPECT_EQ(r.NonZeroCount(), 75);
}

TEST(RangeTest, CardRemovalFullRange) {
  Range r = Range::FullCombinatorial();
  r.RemoveCard(48);
  EXPECT_EQ(r.NonZeroCount(), 1275);
}

TEST(RangeTest, Entropy) {
  EXPECT_GT(Range::FromString("AA").Entropy(), 0.0f);
  Range single;
  single.Set(0, 1.0f);
  EXPECT_FLOAT_EQ(single.Entropy(), 0.0f);
}

TEST(RangeTest, SerializationRoundTrip) {
  Range original = Range::FromString("AA,KK,QQ,AKs,AKo,JTs");
  original.SaveBinary("/tmp/test_range.bin");
  Range loaded = Range::LoadBinary("/tmp/test_range.bin");
  EXPECT_EQ(loaded.NonZeroCount(), original.NonZeroCount());
  for (int i = 0; i < 1326; i++) EXPECT_FLOAT_EQ(loaded.Get(i), original.Get(i));
  std::remove("/tmp/test_range.bin");
}

TEST(RangeTest, SampleReturnsValidHand) {
  Range r = Range::FromString("AA,KK,QQ");
  std::mt19937 rng(42);
  for (int i = 0; i < 100; i++) {
    uint16_t id = r.Sample(rng);
    EXPECT_LT(id, 1326);
  }
}

TEST(RangeTest, CollapseToAbstract) {
  auto a = Range::FromString("AA,AKs,AKo").CollapseToAbstract();
  EXPECT_GT(a[SuitIsomorphism::ToAbstract(48, 51)], 0.0f);
}

TEST(RangeTest, DashRange) {
  Range r = Range::FromString("TT-77");
  EXPECT_EQ(r.NonZeroCount(), 24);
  EXPECT_FLOAT_EQ(r.Get(HandId::Encode(8 * 4, 8 * 4 + 1)), 1.0f);
  EXPECT_FLOAT_EQ(r.Get(HandId::Encode(5 * 4, 5 * 4 + 1)), 1.0f);
  EXPECT_FLOAT_EQ(r.Get(HandId::Encode(4 * 4, 4 * 4 + 1)), 0.0f);
}

TEST(RangeTest, Intersection) {
  Range c = Range::FromString("AA,KK,QQ,AKs") & Range::FromString("AA,KK,AKs,AKo");
  EXPECT_FLOAT_EQ(c.Get(HandId::Encode(48, 51)), 1.0f);
  EXPECT_FLOAT_EQ(c.Get(HandId::Encode(10 * 4, 10 * 4 + 1)), 0.0f);
}

TEST(RangeTest, RangeArithmetic) {
  Range diff = Range::FromString("AA,KK");
  diff -= Range::FromString("KK,QQ");
  EXPECT_FLOAT_EQ(diff.Get(HandId::Encode(12 * 4, 12 * 4 + 1)), 1.0f);
  EXPECT_FLOAT_EQ(diff.Get(HandId::Encode(11 * 4, 11 * 4 + 1)), 0.0f);
}

TEST(RangeTest, EffectiveCombos) {
  EXPECT_NEAR(Range::FullCombinatorial().EffectiveCombos(), 1326.0f, 0.01f);
}

TEST(RangeTest, ComplexRangeParse) {
  Range r = Range::FromString(
      "88+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,ATo+,KTo+,QTo+");
  EXPECT_GT(r.NonZeroCount(), 400);
  EXPECT_LT(r.NonZeroCount(), 1326);
}

// Malformed / illegal range tokens must be rejected gracefully (logged + skipped),
// never abort or silently produce a wrong range. A valid prefix still parses.
TEST(RangeTest, MalformedTokensAreSkipped) {
  // "As-Ks" is nonsensical (dash range must share the high card).
  Range r = Range::FromString("As-Ks,AA");
  // AA parsed correctly; the bad token contributed nothing extra.
  EXPECT_EQ(r.NonZeroCount(), 6);
  EXPECT_FLOAT_EQ(r.Get(HandId::Encode(48, 51)), 1.0f);

  // Invalid modifier / unknown card.
  Range r2 = Range::FromString("AKx,ZZ,QQ");
  EXPECT_EQ(r2.NonZeroCount(), 6);  // only QQ

  // Too-short / empty tokens ignored.
  Range r3 = Range::FromString("A,KK,,");
  EXPECT_EQ(r3.NonZeroCount(), 6);  // only KK

  // Pair-to-nonpair dash mismatch rejected.
  Range r4 = Range::FromString("TT-AKs,22");
  EXPECT_EQ(r4.NonZeroCount(), 6);  // only 22
}
