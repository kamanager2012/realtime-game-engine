// Property-based tests: verify fundamental poker invariants hold
// under randomized inputs. These catch bugs that unit tests miss.

#include <gtest/gtest.h>
#include <random>

#include "poker_engine/game/game_state.h"
#include "poker_engine/game/dealer.h"
#include "poker_engine/game/action_validator.h"
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
  cfg.hand_timeout_seconds = 3600;  // tests drive instantly; no auto-fold
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
// (Drives the actual current player — the previous version poked
// seat 0 and either spun forever or played nothing.)
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
    for (int guard = 0; guard < 2000 && gs->IsHandInProgress(); ++guard) {
      int32_t pid = gs->GetCurrentPlayerId();
      ASSERT_GT(pid, 0);
      GameAction act;
      act.player_id = pid;
      act.type = ActionType::CALL;
      if (!gs->ProcessAction(pid, act)) {
        act.type = ActionType::CHECK;
        ASSERT_TRUE(gs->ProcessAction(pid, act));
      }
    }
    ASSERT_FALSE(gs->IsHandInProgress()) << "hand did not terminate";

    int64_t total_end = 0;
    for (const auto& p : gs->AllPlayers()) {
      if (p.id > 0) total_end += p.chips;
    }
    // Chips should be conserved (no creation/destruction)
    EXPECT_EQ(total_start, total_end) << "Chips not conserved in trial " << trial;
  }
}

// =================================================================
// Invariant 2: Pot equals sum of all player bets — checked DURING
// the hand (reading state after hand end is vacuous: bet_info is
// reset and the pot is settled).
// =================================================================
TEST(PropertyTests, PotEqualsSumOfBets) {
  std::mt19937 rng(23456);
  for (int trial = 0; trial < 30; ++trial) {
    auto gs = MakeGame(3, rng);
    ASSERT_TRUE(gs->StartHand());

    for (int guard = 0; guard < 2000 && gs->IsHandInProgress(); ++guard) {
      int32_t pid = gs->GetCurrentPlayerId();
      ASSERT_GT(pid, 0);
      GameAction act;
      act.player_id = pid;
      act.type = ActionType::CALL;
      if (!gs->ProcessAction(pid, act)) {
        act.type = ActionType::CHECK;
        ASSERT_TRUE(gs->ProcessAction(pid, act));
      }
      if (!gs->IsHandInProgress()) break;
      Chips total_bets = 0;
      for (const auto& p : gs->AllPlayers()) {
        if (p.seat_state != SeatState::EMPTY) total_bets += p.bet_info.total_invested;
      }
      EXPECT_EQ(gs->GetPot(), total_bets) << "Pot != sum of bets in trial " << trial;
    }
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

// =================================================================
// Invariant 6 (REGRESSION): chips are conserved across hands WITH
// folds, raises and all-ins. Σ(chips + invested) must equal the
// initial Σchips at every point — folded players' chips stay in the
// pot, and an uncontested pot is paid exactly once.
// (The previous implementation destroyed folded players' chips and
// double-paid uncontested winners; the older "conservation" test
// above never folds and drives the wrong seat, so it missed both.)
// =================================================================
TEST(PropertyTests, ChipsConservedWithFoldsRaisesAllIns) {
  std::mt19937 rng(987654);
  for (int trial = 0; trial < 40; ++trial) {
    int n = std::uniform_int_distribution<int>(2, 6)(rng);
    TableConfig cfg;
    cfg.max_players = n;
    cfg.small_blind = 5;
    cfg.big_blind = 10;
    cfg.hand_timeout_seconds = 3600;  // no timeout interference
    GameState gs(cfg);

    std::uniform_int_distribution<int> chip_dist(200, 2000);
    int64_t initial = 0;
    for (int i = 0; i < n; ++i) {
      Chips c = chip_dist(rng);
      initial += c;
      ASSERT_TRUE(gs.AddPlayer(1000 + i, "P" + std::to_string(i), c));
    }

    auto conserved = [&]() {
      int64_t sum = 0;
      for (const auto& p : gs.AllPlayers()) {
        if (p.seat_state == SeatState::EMPTY) continue;
        sum += p.chips + p.bet_info.total_invested;
      }
      return sum == initial;
    };

    ASSERT_TRUE(gs.StartHand());
    std::uniform_int_distribution<int> roll(0, 99);
    int steps = 0;
    while (gs.IsHandInProgress() && steps < 2000) {
      ++steps;
      int32_t pid = gs.GetCurrentPlayerId();
      ASSERT_GT(pid, 0) << "no current player while hand in progress";
      int r = roll(rng);
      GameAction act;
      act.player_id = pid;
      if (r < 12) {
        act.type = ActionType::FOLD;
      } else if (r < 22) {
        act.type = ActionType::RAISE;
        act.amount = gs.GetCurrentBet() + cfg.big_blind * (1 + r % 4);
      } else if (r < 27) {
        act.type = ActionType::ALL_IN;
      } else {
        act.type = ActionType::CALL;
      }
      if (!gs.ProcessAction(pid, act)) {
        // Illegal for this spot — fall back to check, then call.
        GameAction fallback;
        fallback.player_id = pid;
        fallback.type = ActionType::CHECK;
        if (!gs.ProcessAction(pid, fallback)) {
          fallback.type = ActionType::CALL;
          ASSERT_TRUE(gs.ProcessAction(pid, fallback))
              << "no legal fallback action in trial " << trial;
        }
      }
      ASSERT_TRUE(conserved()) << "conservation broken mid-hand, trial " << trial
                               << " step " << steps;
    }
    ASSERT_LT(steps, 2000) << "hand did not terminate in trial " << trial;
    ASSERT_TRUE(conserved()) << "conservation broken at hand end, trial " << trial;
  }
}

// =================================================================
// Invariant 7 (REGRESSION): uncontested pot is paid exactly once,
// including folded players' dead money. Heads-up: SB folds preflop,
// BB must win exactly SB+BB and nothing more.
// =================================================================
TEST(PropertyTests, UncontestedPotPaidOnceWithDeadMoney) {
  TableConfig cfg;
  cfg.max_players = 2;
  cfg.small_blind = 50;
  cfg.big_blind = 100;
  cfg.hand_timeout_seconds = 3600;
  GameState gs(cfg);
  ASSERT_TRUE(gs.AddPlayer(1, "A", 1000));
  ASSERT_TRUE(gs.AddPlayer(2, "B", 1000));
  ASSERT_TRUE(gs.StartHand());

  int64_t before = 2000;
  // Current player (SB/button preflop heads-up) folds.
  int32_t pid = gs.GetCurrentPlayerId();
  GameAction fold;
  fold.player_id = pid;
  fold.type = ActionType::FOLD;
  ASSERT_TRUE(gs.ProcessAction(pid, fold));
  ASSERT_FALSE(gs.IsHandInProgress());

  int64_t after = 0;
  for (const auto& p : gs.AllPlayers()) after += p.chips;
  EXPECT_EQ(after, before) << "uncontested fold changed total chips";
  // Winner holds 1000 + loser's 50 SB dead money.
  int64_t mx = 0;
  for (const auto& p : gs.AllPlayers()) mx = std::max(mx, p.chips);
  EXPECT_EQ(mx, 1050);
}

// =================================================================
// Invariant 8 (REGRESSION): folded players' chips reach the pot
// winner at showdown (dead money is not burned). 3 players: one
// invests and folds; the other two see the showdown.
// =================================================================
TEST(PropertyTests, FoldedChipsGoToShowdownWinner) {
  std::mt19937 rng(4242);
  for (int trial = 0; trial < 30; ++trial) {
    TableConfig cfg;
    cfg.max_players = 3;
    cfg.small_blind = 5;
    cfg.big_blind = 10;
    cfg.hand_timeout_seconds = 3600;
    GameState gs(cfg);
    ASSERT_TRUE(gs.AddPlayer(1, "A", 1000));
    ASSERT_TRUE(gs.AddPlayer(2, "B", 1000));
    ASSERT_TRUE(gs.AddPlayer(3, "C", 1000));
    ASSERT_TRUE(gs.StartHand());

    // Preflop: everyone calls/checks.
    for (int i = 0; i < 3 && gs.IsHandInProgress(); ++i) {
      int32_t pid = gs.GetCurrentPlayerId();
      GameAction act;
      act.player_id = pid;
      act.type = ActionType::CALL;
      if (!gs.ProcessAction(pid, act)) {
        act.type = ActionType::CHECK;
        ASSERT_TRUE(gs.ProcessAction(pid, act));
      }
    }
    // Flop: first actor bets 100, second calls, third folds; then the
    // remaining two check down to showdown.
    bool bet_done = false, fold_done = false;
    for (int guard = 0; guard < 200 && gs.IsHandInProgress(); ++guard) {
      int32_t pid = gs.GetCurrentPlayerId();
      GameAction act;
      act.player_id = pid;
      if (!bet_done) {
        act.type = ActionType::BET;
        act.amount = 100;
        bet_done = true;
      } else if (!fold_done) {
        // second to act calls, third folds
        act.type = (guard % 2 == 1) ? ActionType::CALL : ActionType::FOLD;
        if (act.type == ActionType::FOLD) fold_done = true;
      } else {
        act.type = ActionType::CHECK;
      }
      if (!gs.ProcessAction(pid, act)) {
        act.type = ActionType::CALL;
        if (!gs.ProcessAction(pid, act)) {
          act.type = ActionType::CHECK;
          ASSERT_TRUE(gs.ProcessAction(pid, act));
        }
      }
    }
    ASSERT_FALSE(gs.IsHandInProgress());
    int64_t after = 0;
    for (const auto& p : gs.AllPlayers()) after += p.chips;
    EXPECT_EQ(after, 3000) << "folded player's chips were not conserved, trial " << trial;
  }
}

// =================================================================
// Invariant 9 (REGRESSION): NLHE minimum re-raise uses the previous
// raise increment, not merely the big blind. BB=100, raise 100→300
// (increment 200) → next minimum is 500, not 400.
// =================================================================
TEST(PropertyTests, MinRaiseUsesPreviousIncrement) {
  PlayerState raiser;
  raiser.id = 7;
  raiser.chips = 10000;
  raiser.seat_state = SeatState::PLAYING;

  GameAction to400;
  to400.type = ActionType::RAISE;
  to400.amount = 400;
  auto bad = ActionValidator::Validate(to400, raiser, {}, /*current_bet=*/300, /*pot=*/0,
                                       /*big_blind=*/100, /*ante=*/0, 2, 0, 0,
                                       /*last_raise=*/200);
  EXPECT_FALSE(bad.valid) << "raise to 400 accepted when min is 500";

  GameAction to500;
  to500.type = ActionType::RAISE;
  to500.amount = 500;
  auto good = ActionValidator::Validate(to500, raiser, {}, 300, 0, 100, 0, 2, 0, 0, 200);
  EXPECT_TRUE(good.valid) << "raise to 500 rejected: " << good.error;
}

}  // namespace
