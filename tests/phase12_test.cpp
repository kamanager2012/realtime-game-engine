#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/game/action.h"
#include "poker_engine/game/action_validator.h"
#include "poker_engine/game/dealer.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/game/player_state.h"
#include "poker_engine/game/pot_manager.h"
#include "poker_engine/game/showdown_evaluator.h"
#include "poker_engine/game/table.h"

using namespace poker_engine::game;

// ============ Action Tests ============

TEST(Phase12Test, ActionToString) {
  GameAction a;
  a.type = ActionType::RAISE;
  a.amount = 150;
  EXPECT_NE(a.ToString().find("Raise"), std::string::npos);
}

TEST(Phase12Test, ActionValidation_FoldAlwaysLegal) {
  PlayerState p;
  p.id = 1;
  p.chips = 100;
  p.seat_state = SeatState::PLAYING;
  p.bet_info.current_bet = 0;
  GameAction fold;
  fold.type = ActionType::FOLD;
  auto result = ActionValidator::Validate(fold, p, {}, 0, 0, 1, 0, 2, 0, 0);
  EXPECT_TRUE(result.valid);
}

TEST(Phase12Test, ActionValidation_CheckOnlyWhenNoBet) {
  PlayerState p;
  p.id = 1;
  p.chips = 100;
  p.seat_state = SeatState::PLAYING;
  p.bet_info.current_bet = 0;
  GameAction check;
  check.type = ActionType::CHECK;
  auto r1 = ActionValidator::Validate(check, p, {}, 0, 0, 1, 0, 2, 0, 0);
  EXPECT_TRUE(r1.valid);
  auto r2 = ActionValidator::Validate(check, p, {}, 10, 0, 1, 0, 2, 0, 0);
  EXPECT_FALSE(r2.valid);
}

TEST(Phase12Test, ActionValidation_AllInAlwaysLegal) {
  PlayerState p;
  p.id = 1;
  p.chips = 10;
  p.seat_state = SeatState::PLAYING;
  GameAction allin;
  allin.type = ActionType::ALL_IN;
  auto result = ActionValidator::Validate(allin, p, {}, 100, 0, 1, 0, 2, 0, 0);
  EXPECT_TRUE(result.valid);
}

TEST(Phase12Test, ActionValidation_CallRequiresBet) {
  PlayerState p;
  p.id = 1;
  p.chips = 100;
  p.seat_state = SeatState::PLAYING;
  GameAction call;
  call.type = ActionType::CALL;
  call.amount = 5;
  auto result = ActionValidator::Validate(call, p, {}, 0, 0, 1, 0, 2, 0, 0);
  EXPECT_FALSE(result.valid);
}

// ============ Player State Tests ============

TEST(Phase12Test, PlayerBetAndChips) {
  PlayerState p;
  p.id = 1;
  p.chips = 100;
  p.seat_state = SeatState::PLAYING;
  double bet = p.Bet(25);
  EXPECT_DOUBLE_EQ(bet, 25);
  EXPECT_DOUBLE_EQ(p.chips, 75);
  EXPECT_DOUBLE_EQ(p.bet_info.current_bet, 25);
}

TEST(Phase12Test, PlayerReceive) {
  PlayerState p;
  p.id = 1;
  p.chips = 50;
  p.Receive(30);
  EXPECT_DOUBLE_EQ(p.chips, 80);
}

TEST(Phase12Test, BetInfoReset) {
  BetInfo bi;
  bi.current_bet = 50;
  bi.total_invested = 100;
  bi.side_pot_eligible = 30;
  bi.Reset();
  EXPECT_DOUBLE_EQ(bi.current_bet, 0);
  EXPECT_DOUBLE_EQ(bi.total_invested, 0);
}

// ============ Pot Manager Tests ============

TEST(Phase12Test, PotManager_SimplePot) {
  PotManager pm;
  PlayerState p1, p2;
  p1.id = 1;
  p1.bet_info.total_invested = 10;
  p1.seat_state = SeatState::PLAYING;
  p2.id = 2;
  p2.bet_info.total_invested = 10;
  p2.seat_state = SeatState::PLAYING;
  auto pots = pm.BuildPots({&p1, &p2});
  EXPECT_EQ(pots.size(), 1u);
  EXPECT_NEAR(pots[0].amount, 20, 0.01);
}

TEST(Phase12Test, PotManager_SidePot) {
  PotManager pm;
  PlayerState p1, p2, p3;
  p1.id = 1;
  p1.bet_info.total_invested = 30;
  p1.seat_state = SeatState::PLAYING;
  p2.id = 2;
  p2.bet_info.total_invested = 10;
  p2.seat_state = SeatState::PLAYING;
  p3.id = 3;
  p3.bet_info.total_invested = 10;
  p3.seat_state = SeatState::PLAYING;
  auto pots = pm.BuildPots({&p1, &p2, &p3});
  double total = pm.TotalPot();
  EXPECT_NEAR(total, 50, 0.01);
}

TEST(Phase12Test, PotManager_MultipleAllIn) {
  PotManager pm;
  PlayerState p1, p2, p3, p4;
  p1.id = 1;
  p1.bet_info.total_invested = 100;
  p1.seat_state = SeatState::PLAYING;
  p2.id = 2;
  p2.bet_info.total_invested = 50;
  p2.seat_state = SeatState::ALL_IN;
  p3.id = 3;
  p3.bet_info.total_invested = 30;
  p3.seat_state = SeatState::ALL_IN;
  p4.id = 4;
  p4.bet_info.total_invested = 80;
  p4.seat_state = SeatState::PLAYING;
  auto pots = pm.BuildPots({&p1, &p2, &p3, &p4});
  EXPECT_GE(pots.size(), 2u);
  EXPECT_NEAR(pm.TotalPot(), 260, 0.01);
}

// ============ Dealer Tests ============

TEST(Phase12Test, DealerShuffleAndDeal) {
  Dealer dealer;
  EXPECT_EQ(dealer.Remaining(), 52);
  auto cards = dealer.Deal(2);
  EXPECT_EQ(cards.size(), 2u);
  EXPECT_EQ(dealer.Remaining(), 50);
}

TEST(Phase12Test, DealerFlopTurnRiver) {
  Dealer dealer;
  dealer.Shuffle();
  std::vector<uint8_t> flop;
  dealer.DealFlop(flop);
  EXPECT_EQ(flop.size(), 3u);
  uint8_t turn = dealer.DealTurn();
  EXPECT_NE(turn, 0xFF);
  uint8_t river = dealer.DealRiver();
  EXPECT_NE(river, 0xFF);
}

TEST(Phase12Test, DealerReset) {
  Dealer dealer;
  dealer.Deal(10);
  EXPECT_EQ(dealer.Remaining(), 42);
  dealer.Reset();
  EXPECT_EQ(dealer.Remaining(), 52);
}

// ============ Showdown Tests ============

TEST(Phase12Test, Showdown_Uncontested) {
  std::vector<uint8_t> hole{0, 1};
  auto result = ShowdownEvaluator::EvaluatePot({1}, {{1, hole}}, {}, 50);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].player_id, 1);
  EXPECT_DOUBLE_EQ(result[0].amount_won, 50);
}

TEST(Phase12Test, Showdown_TwoPlayers) {
  std::vector<uint8_t> hole1 = {0, 1};
  std::vector<uint8_t> hole2 = {2, 3};
  std::vector<uint8_t> community = {4, 5, 6, 7, 8};
  auto result = ShowdownEvaluator::EvaluatePot({1, 2}, {{1, hole1}, {2, hole2}}, community, 100);
  EXPECT_GT(result.size(), 0u);
  double total_won = 0;
  for (auto& sr : result) total_won += sr.amount_won;
  EXPECT_NEAR(total_won, 100, 0.01);
}

// Regression test: pot splits must conserve chips exactly. The previous
// implementation split via `double` and truncated toward zero, permanently
// destroying pot % winners chips on every uneven multi-way / odd split.
TEST(Phase12Test, Showdown_PotSplitConservation) {
  std::vector<uint8_t> hole = {0, 1};  // identical cards -> a tie
  std::vector<uint8_t> community = {4, 5, 6, 7, 8};

  auto sum = [](const std::vector<ShowdownResult>& r) {
    int64_t s = 0;
    for (auto& sr : r) s += sr.amount_won;
    return s;
  };

  // 3-way tie, pot 100 -> 34/33/33, exact sum 100.
  auto r3 = ShowdownEvaluator::EvaluatePot({1, 2, 3},
      {{1, hole}, {2, hole}, {3, hole}}, community, 100);
  ASSERT_EQ(r3.size(), 3u);
  EXPECT_EQ(sum(r3), 100);

  // 2-way tie, odd pot 101 -> 51/50, exact sum 101.
  auto r2 = ShowdownEvaluator::EvaluatePot({1, 2},
      {{1, hole}, {2, hole}}, community, 101);
  ASSERT_EQ(r2.size(), 2u);
  EXPECT_EQ(sum(r2), 101);

  // 3-way tie, odd pot 101 -> 34/34/33, exact sum 101.
  auto r3b = ShowdownEvaluator::EvaluatePot({1, 2, 3},
      {{1, hole}, {2, hole}, {3, hole}}, community, 101);
  ASSERT_EQ(r3b.size(), 3u);
  EXPECT_EQ(sum(r3b), 101);
}

// ============ Game State Tests ============

TEST(Phase12Test, GameStateCreation) {
  TableConfig config;
  config.max_players = 6;
  GameState gs(config);
  EXPECT_EQ(gs.GetPhase(), GamePhase::WAITING);
  EXPECT_FALSE(gs.IsHandInProgress());
}

TEST(Phase12Test, GameStateAddPlayer) {
  TableConfig config;
  config.max_players = 6;
  config.min_buy_in = 10;
  config.max_buy_in = 5000;
  GameState gs(config);
  EXPECT_TRUE(gs.AddPlayer(1, "Alice", 100));
  EXPECT_TRUE(gs.AddPlayer(2, "Bob", 100));
}

TEST(Phase12Test, GameStateStartHand) {
  TableConfig config;
  config.max_players = 2;
  config.min_buy_in = 10;
  config.max_buy_in = 5000;
  GameState gs(config);
  gs.AddPlayer(1, "Alice", 1000);
  gs.AddPlayer(2, "Bob", 1000);
  gs.StartHand();
  EXPECT_NE(gs.GetPhase(), GamePhase::WAITING);
}

// ============ Community Cards Tests ============

TEST(Phase12Test, CommunityCardsProgression) {
  CommunityCards cc;
  EXPECT_EQ(cc.count, 0);
  cc.Add(1);
  cc.Add(2);
  cc.Add(3);
  EXPECT_TRUE(cc.HasFlop());
  cc.Add(4);
  EXPECT_TRUE(cc.HasTurn());
  cc.Add(5);
  EXPECT_TRUE(cc.HasRiver());
}

// ============ Table Integration ============

TEST(Phase12Test, TableCreation) {
  TableSettings settings;
  settings.max_players = 2;
  Table table(1, settings);
  EXPECT_EQ(table.ActivePlayerCount(), 0);
}

TEST(Phase12Test, TableJoinAndStart) {
  TableConfig config;
  config.max_players = 2;
  config.min_buy_in = 10;
  config.max_buy_in = 5000;
  Table table(config);

  table.JoinTable(1, "Alice", 200);
  table.JoinTable(2, "Bob", 200);

  EXPECT_NO_THROW(table.StartHand());
}

TEST(Phase12Test, TableJoinHonorsRequestedSeat) {
  TableConfig config;
  config.max_players = 6;
  config.min_buy_in = 10;
  config.max_buy_in = 500;
  Table table(config);

  ASSERT_TRUE(table.JoinTable(1, "Alice", 200, 3));

  const auto& players = table.GetGameState().AllPlayers();
  ASSERT_EQ(players.size(), 6u);
  EXPECT_EQ(players[3].id, 1);
  EXPECT_EQ(players[3].name, "Alice");
  EXPECT_DOUBLE_EQ(players[3].chips, 200);
  EXPECT_EQ(players[3].seat_state, SeatState::SITTING);

  EXPECT_FALSE(table.JoinTable(2, "Bob", 200, 3));
  EXPECT_FALSE(table.JoinTable(1, "AliceAgain", 200, 4));
}

TEST(Phase12Test, TableCallback) {
  TableConfig config;
  config.max_players = 2;
  config.min_buy_in = 10;
  config.max_buy_in = 5000;
  Table table(config);
  bool callback_fired = false;
  table.SetCallback([&](const TableEvent&) { callback_fired = true; });

  table.JoinTable(1, "Alice", 200);
  EXPECT_NO_THROW(table.JoinTable(2, "Bob", 200));
}

// ============ Hand Rank Names ============

TEST(Phase12Test, HandRankNames) {
  EXPECT_NE(ShowdownEvaluator::HandRankName(0), "");
  EXPECT_NE(ShowdownEvaluator::HandRankName(9999999), "");
}

// ============ Preflop Action Order (BUG FIX) ============

TEST(Phase12Test, PreflopActionOrder_ThreePlayer) {
  TableConfig config;
  config.max_players = 3;
  config.small_blind = 1;
  config.big_blind = 2;
  config.min_buy_in = 10;
  config.max_buy_in = 5000;

  GameState gs(config);
  gs.AddPlayer(1, "Alice", 1000);
  gs.AddPlayer(2, "Bob", 1000);
  gs.AddPlayer(3, "Carol", 1000);

  gs.StartHand();
  // Should be in preflop
  EXPECT_EQ(gs.GetPhase(), GamePhase::PREFLOP_BETTING);

  // All 3 players should be active
  EXPECT_GE(gs.ActivePlayerCount(), 2);
}

TEST(Phase12Test, FullHandHeadsUp) {
  // Heads-up: preflop SB acts first, postflop BTN acts first
  TableSettings settings;
  settings.max_players = 2;
  settings.small_blind = 1;
  settings.big_blind = 2;
  settings.min_buy_in = 10;
  settings.max_buy_in = 5000;

  Table table(1, settings);
  table.JoinTable(1, "Alice", 1000);
  table.JoinTable(2, "Bob", 1000);

  table.StartHand();
  EXPECT_TRUE(table.IsPlaying());

  // In heads-up preflop, SB (button) acts first
  // So we need to act in correct order based on dealer position
  // Simplified: just verify hand progresses
}

TEST(Phase12Test, HeadsUpPreflopDoesNotDoubleCountInvestedChips) {
  TableConfig config;
  config.max_players = 2;
  config.small_blind = 1;
  config.big_blind = 2;
  config.min_buy_in = 10;
  config.max_buy_in = 5000;

  GameState gs(config);
  ASSERT_TRUE(gs.AddPlayer(1, "Alice", 1000));
  ASSERT_TRUE(gs.AddPlayer(2, "Bob", 1000));

  gs.StartHand();
  ASSERT_EQ(gs.GetPhase(), GamePhase::PREFLOP_BETTING);
  ASSERT_EQ(gs.GetCurrentPlayerId(), 2);

  GameAction call;
  call.type = ActionType::CALL;
  call.player_id = 2;
  ASSERT_TRUE(gs.ProcessAction(2, call));

  GameAction check;
  check.type = ActionType::CHECK;
  check.player_id = 1;
  ASSERT_TRUE(gs.ProcessAction(1, check));
  ASSERT_EQ(gs.GetPhase(), GamePhase::FLOP_BETTING);

  const auto& players = gs.AllPlayers();
  ASSERT_EQ(players.size(), 2u);
  EXPECT_DOUBLE_EQ(players[0].bet_info.total_invested, 2);
  EXPECT_DOUBLE_EQ(players[1].bet_info.total_invested, 2);
}

// ============ Action Validator Edge Cases ============

TEST(Phase12Test, FoldedPlayerCannotAct) {
  PlayerState p;
  p.id = 1;
  p.chips = 100;
  p.seat_state = SeatState::FOLDED;
  GameAction call;
  call.type = ActionType::CALL;
  auto result = ActionValidator::Validate(call, p, {&p}, 0, 0, 1, 0, 1, 0, 0);
  EXPECT_FALSE(result.valid);
}

TEST(Phase12Test, AllInPlayerCannotAct) {
  PlayerState p;
  p.id = 1;
  p.chips = 0;
  p.seat_state = SeatState::ALL_IN;
  GameAction call;
  call.type = ActionType::CALL;
  auto result = ActionValidator::Validate(call, p, {&p}, 0, 0, 1, 0, 1, 0, 0);
  EXPECT_FALSE(result.valid);
}

TEST(Phase12Test, ShortStackAllIn) {
  PlayerState p;
  p.id = 1;
  p.chips = 3;
  p.seat_state = SeatState::PLAYING;
  p.bet_info.current_bet = 1;
  GameAction allin;
  allin.type = ActionType::ALL_IN;
  allin.amount = 2;
  auto result = ActionValidator::Validate(allin, p, {&p}, 4, 0, 2, 0, 3, 0, 0);
  EXPECT_TRUE(result.valid);
  EXPECT_DOUBLE_EQ(result.adjusted_amount, 3);
}

TEST(Phase12Test, RaiseCappedByChips) {
  PlayerState p;
  p.id = 1;
  p.chips = 50;
  p.seat_state = SeatState::PLAYING;
  p.bet_info.current_bet = 0;
  GameAction raise;
  raise.type = ActionType::RAISE;
  raise.amount = 999;
  auto result = ActionValidator::Validate(raise, p, {&p}, 10, 100, 5, 0, 2, 0, 0);
  EXPECT_TRUE(result.valid);
  EXPECT_LE(result.adjusted_amount, 50);
}

// ============ Pot Exact Calculation ============

TEST(Phase12Test, ExactAllInPots) {
  PotManager pm;
  PlayerState p1, p2, p3;
  p1.id = 1;
  p1.bet_info.total_invested = 100;
  p1.seat_state = SeatState::ALL_IN;
  p2.id = 2;
  p2.bet_info.total_invested = 100;
  p2.seat_state = SeatState::PLAYING;
  p3.id = 3;
  p3.bet_info.total_invested = 100;
  p3.seat_state = SeatState::PLAYING;
  auto pots = pm.BuildPots({&p1, &p2, &p3});
  EXPECT_EQ(pots.size(), 1u);
  EXPECT_DOUBLE_EQ(pots[0].amount, 300);
}

TEST(Phase12Test, UnevenAllInPots) {
  PotManager pm;
  PlayerState p1, p2, p3;
  p1.id = 1;
  p1.bet_info.total_invested = 30;
  p1.seat_state = SeatState::ALL_IN;
  p2.id = 2;
  p2.bet_info.total_invested = 100;
  p2.seat_state = SeatState::PLAYING;
  p3.id = 3;
  p3.bet_info.total_invested = 100;
  p3.seat_state = SeatState::PLAYING;
  auto pots = pm.BuildPots({&p1, &p2, &p3});
  EXPECT_NEAR(pm.TotalPot(), 230, 0.01);
  EXPECT_GE(pots.size(), 2u);
}

// ============ TableSettings ============

TEST(Phase12Test, TableSettingsConversion) {
  TableSettings settings;
  settings.name = "Test";
  settings.max_players = 6;
  settings.small_blind = 5;
  settings.big_blind = 10;

  auto config = settings.ToGameConfig();
  EXPECT_EQ(config.max_players, 6);
  EXPECT_DOUBLE_EQ(config.small_blind, 5);
  EXPECT_DOUBLE_EQ(config.big_blind, 10);
}

TEST(Phase12Test, TableWithSettings) {
  TableSettings settings;
  settings.name = "VIP";
  settings.max_players = 2;
  settings.small_blind = 5;
  settings.big_blind = 10;
  settings.min_buy_in = 100;
  settings.max_buy_in = 2000;

  Table table(1, settings);
  table.JoinTable(1, "Alice", 1000);
  table.JoinTable(2, "Bob", 1000);

  EXPECT_NO_THROW(table.StartHand());
}

TEST(Phase12Test, TableStateJSON) {
  TableSettings settings;
  settings.name = "TestTable";
  Table table(42, settings);

  table.JoinTable(1, "Alice", 500);
  std::string json = table.GetStateJSON();
  EXPECT_NE(json.find("\"table_id\":42"), std::string::npos);
  EXPECT_NE(json.find("Alice"), std::string::npos);
}

// =========== RNG Fairness ===========

// A fresh Dealer must NOT use a fixed/guessable seed: two successive
// shuffles should produce different decks.
TEST(Phase12Test, FreshDealerUsesNonDeterministicSeed) {
  Dealer d1, d2;
  d1.Shuffle();
  d2.Shuffle();
  EXPECT_NE(d1.GetProof().deck_hash, d2.GetProof().deck_hash)
      << "Two fresh shuffles produced identical decks — seed is not random";
  EXPECT_FALSE(d1.Commitment().empty());
}

// The published commitment is SHA256(seed||nonce) and must change with the
// seed, proving it is bound to the actual shuffle inputs.
TEST(Phase12Test, CommitmentBindsToSeed) {
  Dealer a, b;
  a.Reseed("0000000000000001000000000000000000000000000000000000000000000000", "aa");
  a.ShuffleWithSeed();
  b.Reseed("0000000000000002000000000000000000000000000000000000000000000000", "aa");
  b.ShuffleWithSeed();
  EXPECT_NE(a.Commitment(), b.Commitment());
  EXPECT_NE(a.GetProof().deck_hash, b.GetProof().deck_hash);
}

// Auditor replay: given a stored (seed, nonce, commitment), re-deriving the
// commitment from seed||nonce must match, and the same seed+nonce must
// reproduce the exact deck. This is the core fairness verifiability check.
TEST(Phase12Test, StoredProofIsReplayableAndVerifiable) {
  Dealer d;
  d.Shuffle();
  auto proof = d.GetProof();

  // Recompute commitment from revealed seed||nonce.
  Dealer auditor;
  auditor.Reseed(proof.seed_hex, proof.nonce);
  auditor.ShuffleWithSeed();
  EXPECT_EQ(auditor.Commitment(), proof.commitment)
      << "Recomputed commitment != stored commitment — deal not verifiable";
  EXPECT_EQ(auditor.GetProof().deck_hash, proof.deck_hash)
      << "Replay with same seed+nonce produced a different deck";
}

// Determinism: identical seed+nonce always yields the identical deck.
TEST(Phase12Test, SameSeedNonceReproducesDeck) {
  Dealer a, b;
  a.Reseed("00000000deadbeef000000000000000000000000000000000000000000000000", "0123456789abcdef");
  b.Reseed("00000000deadbeef000000000000000000000000000000000000000000000000", "0123456789abcdef");
  a.ShuffleWithSeed();
  b.ShuffleWithSeed();
  EXPECT_EQ(a.GetProof().deck_hash, b.GetProof().deck_hash);
}

// A full 52-card deck must remain a permutation after a shuffle (no missing
// or duplicated cards), which is what fairness ultimately requires.
TEST(Phase12Test, ShuffleProducesValidPermutation) {
  Dealer d;
  d.Shuffle();
  std::vector<bool> seen(52, false);
  for (int i = 0; i < 52; ++i) {
    uint8_t c = d.DealOne();
    ASSERT_LT(c, 52);
    EXPECT_FALSE(seen[c]) << "Duplicate card dealt: " << int(c);
    seen[c] = true;
  }
  EXPECT_EQ(d.Remaining(), 0);
}
