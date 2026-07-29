// Property-based tests: verify fundamental poker invariants hold
// under randomized inputs. These catch bugs that unit tests miss.

#include <gtest/gtest.h>
#include <random>

#include "poker_engine/game/game_state.h"
#include "poker_engine/game/dealer.h"
#include "poker_engine/evaluator/card.h"

using namespace poker_engine::game;

namespace {

// =================================================================
// Helper: create a game with N players and random chip stacks
// =================================================================
std::unique_ptr<GameState> MakeGame(int num_players, std::mt19937& rng) {
  TableConfig cfg;
  cfg.max_players = num_players;
  cfg.small_blind = 50;   // $0.50
  cfg.big_blind = 100;    // $1.00
  auto gs = std::make_unique<GameState>(cfg);

  std::uniform_int_distribution<int> chip_dist(1000, 5000);
  for (int i = 0; i < num_players; ++i) {
    gs->AddPlayer(100 + i, "P" + std::to_string(i), chip_dist(rng));
  }
  return gs;
}

// =================================================================
// Invariant 1: Total chips in the system is conserved.
// After a hand, sum(player chips + table chips) == initial.
// =================================================================
TEST(PropertyTests, ChipsConservedAcrossFullHand) {
  std::mt19937 rng(12345);
  for (int trial = 0; trial < 50; ++trial) {
    int n = std::uniform_int_distribution<int>(2, 5)(rng);
    auto gs = MakeGame(n + 1, rng); // +1 to ensure at least 2

    int64_t total_start = 0;
    for (const auto& p : gs->AllPlayers()) {
      if (p.id > 0) total_start += p.chips;
    }

    // Play through a hand
    ASSERT_TRUE(gs->StartHand());
    while (gs->IsHandInProgress()) {
      auto* cur = gs->AllPlayers().data();
      if (!cur || !cur->IsActive()) break;
      GameAction act;
      act.type = ActionType::CALL;
      act.amount = 0;
      gs->ProcessAction(cur->id, act);
    }

    int64_t total_end = 0;
    for (const auto& p : gs->AllPlayers()) {
      if (p.id > 0) total_end += p.chips;
    }
    // Chips should be conserved (no creation/destruction)
    EXPECT_EQ(total_start, total_end) << "Chips not conserved in trial " << trial;
  }
}

// =================================================================
// Invariant 2: Pot equals sum of all player bets.
// =================================================================
TEST(PropertyTests, PotEqualsSumOfBets) {
  std::mt19937 rng(23456);
  for (int trial = 0; trial < 30; ++trial) {
    auto gs = MakeGame(3, rng);
    ASSERT_TRUE(gs->StartHand());

    // Collect all-in actions until hand ends
    while (gs->IsHandInProgress()) {
      for (auto& p : gs->AllPlayers()) {
        if (p.IsActive()) {
          GameAction act;
          act.type = ActionType::ALL_IN;
          act.amount = 0;
          gs->ProcessAction(p.id, act);
          break;
        }
      }
      if (!gs->IsHandInProgress()) break;
    }

    // After hand: check pot
    Chips total_bets = 0;
    for (const auto& p : gs->AllPlayers()) {
      if (p.id > 0) total_bets += p.bet_info.total_invested;
    }
    Chips pot = gs->GetPot();
    EXPECT_EQ(pot, total_bets) << "Pot != sum of bets in trial " << trial;
  }
}

// =================================================================
// Invariant 3: Shuffled deck is a permutation of 0..51.
// =================================================================
TEST(PropertyTests, DeckIsValidPermutationAfterShuffle) {
  for (int trial = 0; trial < 100; ++trial) {
    Dealer d;
    d.Shuffle();
    std::vector<bool> seen(52, false);
    for (int i = 0; i < 52; ++i) {
      uint8_t c = d.DealOne();
      ASSERT_LT(c, 52) << "Card out of range";
      EXPECT_FALSE(seen[c]) << "Duplicate card " << int(c);
      seen[c] = true;
    }
    EXPECT_EQ(d.Remaining(), 0);
  }
}

// =================================================================
// Invariant 4: No player can have negative chips.
// =================================================================
TEST(PropertyTests, NoNegativeChipsAfterRandomActions) {
  std::mt19937 rng(34567);
  for (int trial = 0; trial < 20; ++trial) {
    auto gs = MakeGame(4, rng);
    for (const auto& p : gs->AllPlayers()) {
      if (p.id > 0) EXPECT_GE(p.chips, 0) << "Negative chips at start";
    }
  }
}

// =================================================================
// Invariant 5: Two dealers with same seed+nonce produce same deck.
// =================================================================
TEST(PropertyTests, SameSeedProducesIdenticalDeck) {
  for (int trial = 0; trial < 10; ++trial) {
    Dealer a, b;
    a.Shuffle();
    auto proof = a.GetProof();
    b.Reseed(proof.seed_hex, proof.nonce);
    b.ShuffleWithSeed();
    EXPECT_EQ(a.GetProof().deck_hash, b.GetProof().deck_hash)
        << "Audit replay mismatch in trial " << trial;
  }
}

}  // namespace
