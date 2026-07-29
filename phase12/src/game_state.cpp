#include "poker_engine/game/game_state.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#include "poker_engine/base/logging.h"
#include "poker_engine/game/action_validator.h"

namespace poker_engine::game {

GameState::GameState(const TableConfig& config) : config_(config) {
  players_.resize(config.max_players);
}

// ========== Table management ==========

bool GameState::AddPlayer(int32_t player_id, const std::string& name, Chips chips) {
  for (uint8_t seat = 0; seat < config_.max_players; seat++) {
    if (players_[seat].seat_state == SeatState::EMPTY) {
      return AddPlayerAtSeat(player_id, name, chips, seat);
    }
  }
  return false;
}

bool GameState::AddPlayerAtSeat(int32_t player_id, const std::string& name, Chips chips,
                                uint8_t seat) {
  if (seat >= config_.max_players) return false;
  auto& p = players_[seat];
  if (p.seat_state != SeatState::EMPTY) return false;
  for (const auto& existing : players_) {
    if (existing.id == player_id && existing.seat_state != SeatState::EMPTY) return false;
  }
  p.id = player_id;
  p.name = name;
  p.chips = chips;
  p.seat = seat;
  p.seat_state = SeatState::SITTING;
  EmitEvent(GameEvent::PLAYER_JOINED, name + " sat at seat " + std::to_string(seat + 1));
  return true;
}

bool GameState::RemovePlayer(int32_t player_id) {
  for (auto& p : players_) {
    if (p.id == player_id && p.seat_state != SeatState::EMPTY) {
      std::string nm = p.name;
      if (p.IsPlaying() && IsHandInProgress()) {
        // Grace period: mark disconnected, don't fold immediately.
        // Auto-fold will happen after grace period expires (checked in ProcessAction).
        disconnects_[player_id] = std::chrono::steady_clock::now();
        EmitEvent(GameEvent::ACTION_TAKEN, nm + " disconnected (grace period)");
        return true;  // keep seat occupied, player may return
      }
      if (p.IsPlaying()) {
        p.seat_state = SeatState::FOLDED;
        num_folded_++;
        num_active_--;
        p.acted_this_round = true;
        EmitEvent(GameEvent::ACTION_TAKEN, nm + " folds (left table)");
        if (IsHandInProgress()) {
          if (IsBettingRoundComplete()) {
            EndBettingRound();
            AdvanceStreet();
          } else {
            NextActionSeat();
          }
        }
      } else {
        EmitEvent(GameEvent::PLAYER_LEFT, nm + " left");
      }
      p.seat_state = SeatState::EMPTY;
      p.id = 0;
      p.name = "";
      p.hole_cards.Reset();
      p.bet_info.Reset();
      return true;
    }
  }
  return false;
}

void GameState::ReconnectPlayer(int32_t player_id) {
  auto it = disconnects_.find(player_id);
  if (it != disconnects_.end()) {
    disconnects_.erase(it);
  }
}

bool GameState::SitDown(int32_t player_id, uint8_t seat) {
  if (seat >= config_.max_players) return false;
  auto& p = players_[seat];
  if (p.seat_state != SeatState::EMPTY) return false;
  for (const auto& existing : players_) {
    if (existing.id == player_id && existing.seat_state != SeatState::EMPTY) return false;
  }
  p.id = player_id;
  p.seat = seat;
  p.seat_state = SeatState::SITTING;
  return true;
}

bool GameState::StandUp(int32_t player_id) {
  for (auto& p : players_) {
    if (p.id == player_id && !p.IsPlaying()) {
      p.seat_state = SeatState::EMPTY;
      p.id = 0;
      p.name = "";
      return true;
    }
  }
  return false;
}

// ========== Core game flow ==========

bool GameState::StartHand() {
  std::lock_guard<std::recursive_mutex> lock(state_mutex_);
  if (phase_ != GamePhase::WAITING && phase_ != GamePhase::HAND_COMPLETE &&
      phase_ != GamePhase::ERROR) {
    EmitEvent(GameEvent::ERROR, "Hand already in progress");
    return false;
  }

  // Need at least 2 sitting players
  int sitting = 0;
  for (const auto& p : players_) {
    if (p.seat_state == SeatState::SITTING || p.seat_state == SeatState::SITTING_OUT) sitting++;
  }
  if (sitting < 2) {
    EmitEvent(GameEvent::ERROR, "Not enough players (need 2+)");
    return false;
  }

  ResetHand();
  hand_started_ = true;
  phase_ = GamePhase::DEALING;
  hand_counter_++;

  EmitEvent(GameEvent::HAND_STARTED, "Hand #" + std::to_string(hand_counter_) + " started");

  RotateDealer();
  PostBlinds();
  DealHoleCards();

  phase_ = GamePhase::PREFLOP_BETTING;
  StartBettingRound();

  return true;
}

bool GameState::ProcessAction(int32_t player_id, const GameAction& action) {
  std::lock_guard<std::recursive_mutex> lock(state_mutex_);
  if (!IsHandInProgress()) return false;

  // Check timeout before processing any action — auto-fold stale players.
  CheckTimeout();
  if (!IsHandInProgress()) return false;  // timeout may have ended the hand

  // Check disconnect grace period — auto-fold players past the limit.
  {
    constexpr auto kDisconnectGrace = std::chrono::seconds(60);
    auto now = std::chrono::steady_clock::now();
    for (auto it = disconnects_.begin(); it != disconnects_.end(); ) {
      if (now - it->second > kDisconnectGrace) {
        // Find and fold this player.
        for (auto& p : players_) {
          if (p.id == it->first && p.IsActive()) {
            p.seat_state = SeatState::FOLDED;
            num_folded_++;
            num_active_--;
            EmitActionEvent(p.id, ActionType::FOLD, 0, p.name + " folds (disconnect timeout)");
            break;
          }
        }
        it = disconnects_.erase(it);
        if (IsBettingRoundComplete()) {
          EndBettingRound();
          AdvanceStreet();
          if (!IsHandInProgress()) return false;
        } else {
          NextActionSeat();
        }
      } else {
        ++it;
      }
    }
  }

  if (!IsHandInProgress()) return false;

  // Find player by id
  PlayerState* player = nullptr;
  for (auto& p : players_) {
    if (p.id == player_id && p.IsActive()) {
      player = &p;
      break;
    }
  }
  if (!player) {
    EmitEvent(GameEvent::ERROR, "Player not found or not active");
    return false;
  }

  // Check if it's this player's turn
  PlayerState* current = GetCurrentPlayer();
  if (!current || current->id != player_id) {
    EmitEvent(GameEvent::ERROR,
              "Not your turn (current: " + (current ? std::to_string(current->id) : "none") + ")");
    return false;
  }

  // Build active list for validation
  std::vector<PlayerState*> active_ptrs;
  for (auto& p : players_) {
    if (p.IsActive() || p.IsAllIn()) active_ptrs.push_back(&p);
  }

  int all_in_count = 0;
  for (const auto* p : active_ptrs) {
    if (p->IsAllIn()) all_in_count++;
  }

  Chips to_call = current_bet_ - player->bet_info.current_bet;
  if (to_call < 0) to_call = 0;

  // Validate action
  auto validation = ActionValidator::Validate(
      action, *player, active_ptrs, current_bet_, GetPot(), config_.big_blind, config_.ante,
      ActivePlayerCount(), all_in_count,
      static_cast<int>(phase_) - static_cast<int>(GamePhase::PREFLOP_BETTING));

  if (!validation.valid) {
    EmitEvent(GameEvent::ERROR, player->name + ": Invalid - " + validation.error);
    return false;
  }

  // Apply action
  Chips amount = validation.adjusted_amount;
  player->acted_this_round = true;

  switch (action.type) {
    case ActionType::FOLD: {
      player->seat_state = SeatState::FOLDED;
      num_folded_++;
      num_active_--;
      EmitActionEvent(player_id, ActionType::FOLD, 0, player->name + " folds");
      break;
    }

    case ActionType::CHECK: {
      EmitActionEvent(player_id, ActionType::CHECK, 0, player->name + " checks");
      break;
    }

    case ActionType::CALL: {
      Chips call_needed = current_bet_ - player->bet_info.current_bet;
      Chips called = player->Bet(call_needed);
      if (player->chips <= 0) {  // exact integer comparison
        player->seat_state = SeatState::ALL_IN;
        num_all_in_++;
        num_active_--;
        EmitActionEvent(player_id, ActionType::CALL, called,
                  player->name + " calls $" + std::to_string(int(called)) + " (ALL-IN)");
      } else {
        EmitActionEvent(player_id, ActionType::CALL, called, player->name + " calls $" + std::to_string(int(called)));
      }
      break;
    }

    case ActionType::BET: {
      Chips bet = player->Bet(amount);
      current_bet_ = player->bet_info.current_bet;
      actions_since_last_bet_ = 0;
      // Reset others' acted flags since new bet
      for (auto& p : players_) {
        if (p.id != player_id && p.IsActive()) p.acted_this_round = false;
      }
      EmitActionEvent(player_id, ActionType::BET, bet, player->name + " bets $" + std::to_string(int(bet)));
      break;
    }

    case ActionType::RAISE: {
      Chips needed = amount - player->bet_info.current_bet;
      if (needed < 0) needed = 0;
      Chips raised = player->Bet(needed);
      current_bet_ = player->bet_info.current_bet;
      actions_since_last_bet_ = 0;
      for (auto& p : players_) {
        if (p.id != player_id && p.IsActive()) p.acted_this_round = false;
      }
      EmitActionEvent(player_id, ActionType::RAISE, raised,
                player->name + " raises to $" + std::to_string(int(current_bet_)));
      break;
    }

    case ActionType::ALL_IN: {
      Chips allin = player->Bet(player->chips);
      if (player->bet_info.current_bet > current_bet_) {
        current_bet_ = player->bet_info.current_bet;
        actions_since_last_bet_ = 0;
        for (auto& p : players_) {
          if (p.id != player_id && p.IsActive()) p.acted_this_round = false;
        }
      }
      player->seat_state = SeatState::ALL_IN;
      num_all_in_++;
      num_active_--;
      EmitActionEvent(player_id, ActionType::ALL_IN, allin, player->name + " all-in $" + std::to_string(int(allin)));
      break;
    }

    default:
      EmitEvent(GameEvent::ERROR, "Unknown action");
      return false;
  }

  CheckCapEffect();

  // Check if betting round is complete
  if (IsBettingRoundComplete()) {
    EndBettingRound();
    AdvanceStreet();
  } else {
    NextActionSeat();
  }

  return true;
}

bool GameState::CheckTimeout() {
  if (!IsHandInProgress()) return false;
  if (action_deadline_.time_since_epoch().count() == 0) return false;

  auto now = std::chrono::steady_clock::now();
  if (now < action_deadline_) return false;

  PlayerState* current = GetCurrentPlayer();
  if (!current) return false;

  // Auto-fold the player who exceeded the timeout.
  PE_LOG_WARN("Action timeout: auto-folding player {} ({}s limit)",
              current->id, config_.hand_timeout_seconds);

  // Force-fold via internal fold path.
  current->seat_state = SeatState::FOLDED;
  num_folded_++;
  num_active_--;
  EmitActionEvent(current->id, ActionType::FOLD, 0,
                  current->name + " folds (timeout)");

  CheckCapEffect();
  if (IsBettingRoundComplete()) {
    EndBettingRound();
    AdvanceStreet();
  } else {
    NextActionSeat();
  }
  return true;
}

void GameState::AdvanceStreet() {
  // Count active (not folded, not all-in) players
  int active_not_allin = 0;
  int total_playing = 0;  // active + all-in
  for (const auto& p : players_) {
    if (p.IsActive()) {
      active_not_allin++;
      total_playing++;
    } else if (p.IsAllIn())
      total_playing++;
  }

  // Only one player remaining (everyone else folded)
  if (total_playing <= 1) {
    // Award pot to last player
    int32_t winner_id = -1;
    for (const auto& p : players_) {
      if (p.IsActive() || p.IsAllIn()) {
        winner_id = p.id;
        break;
      }
    }
    if (winner_id != -1) {
      Chips pot = GetPot();
      for (auto& p : players_) {
        if (p.id == winner_id) {
          p.Receive(pot);
          EmitEvent(GameEvent::PAYOUT,
                    p.name + " wins $" + std::to_string(int(pot)) + " (uncontested)");
        }
      }
    }
    DoShowdown();
    return;
  }

  // No more active players (all are all-in) → deal remaining cards and showdown
  if (active_not_allin == 0) {
    // Deal remaining community cards
    while (community_.count < 5) {
      if (community_.count == 0)
        DealCommunity(3);
      else
        DealCommunity(1);
    }
    EmitEvent(GameEvent::COMMUNITY_DEALT, "Runout: " + community_.CardsStr());
    DoShowdown();
    return;
  }

  // Reset per-street state
  ResetStreet();
  for (auto& p : players_) {
    p.bet_info.current_bet = 0;
  }
  current_bet_ = 0;

  switch (phase_) {
    case GamePhase::PREFLOP_BETTING:
      phase_ = GamePhase::FLOP_DEALING;
      DealFlop();
      phase_ = GamePhase::FLOP_BETTING;
      EmitEvent(GameEvent::COMMUNITY_DEALT, "Flop: " + community_.CardsStr());
      break;

    case GamePhase::FLOP_BETTING:
      phase_ = GamePhase::TURN_DEALING;
      DealTurn();
      phase_ = GamePhase::TURN_BETTING;
      EmitEvent(GameEvent::COMMUNITY_DEALT, "Turn: " + community_.CardsStr());
      break;

    case GamePhase::TURN_BETTING:
      phase_ = GamePhase::RIVER_DEALING;
      DealRiver();
      phase_ = GamePhase::RIVER_BETTING;
      EmitEvent(GameEvent::COMMUNITY_DEALT, "River: " + community_.CardsStr());
      break;

    case GamePhase::RIVER_BETTING:
      DoShowdown();
      return;

    default:
      return;
  }

  StartBettingRound();
}

// ========== Fixed: Preflop starting position ==========

void GameState::StartBettingRound() {
  actions_since_last_bet_ = 0;

  // If only <=1 active player, skip
  if (ActivePlayerCount() <= 1) {
    EndBettingRound();
    AdvanceStreet();
    return;
  }

  // Determine first actor based on street
  uint8_t start;
  if (phase_ == GamePhase::PREFLOP_BETTING) {
    // Preflop: UTG = BB + 1 = dealer + 3 (in 3+ player)
    // Heads-up: BTN/SB acts first preflop
    int total = GetActivePlayerCount();
    if (total == 2) {
      start = dealer_seat_;  // heads-up: button acts first preflop
    } else {
      start = (dealer_seat_ + 3) % config_.max_players;
    }
  } else {
    // Postflop: first active player after dealer
    start = (dealer_seat_ + 1) % config_.max_players;
  }

  FindActiveSeat(start);

  // Set action timeout deadline for the first player to act.
  action_deadline_ = std::chrono::steady_clock::now() +
                     std::chrono::seconds(config_.hand_timeout_seconds);
  timeout_player_id_ = players_[action_seat_].id;
}

void GameState::FindActiveSeat(uint8_t from_seat) {
  for (int i = 0; i < config_.max_players; i++) {
    uint8_t seat = (from_seat + i) % config_.max_players;
    if (players_[seat].IsActive() && !players_[seat].acted_this_round) {
      action_seat_ = seat;
      return;
    }
  }
  // All have acted → find next active anyway
  for (int i = 0; i < config_.max_players; i++) {
    uint8_t seat = (from_seat + i) % config_.max_players;
    if (players_[seat].IsActive()) {
      action_seat_ = seat;
      return;
    }
  }
}

// ========== Private methods ==========

void GameState::RotateDealer() {
  for (int i = 1; i <= config_.max_players; i++) {
    uint8_t seat = (dealer_seat_ + i) % config_.max_players;
    if (players_[seat].seat_state == SeatState::SITTING ||
        players_[seat].seat_state == SeatState::PLAYING) {
      dealer_seat_ = seat;
      break;
    }
  }
  for (auto& p : players_) {
    p.is_dealer = false;
    p.is_small_blind = false;
    p.is_big_blind = false;
  }
  players_[dealer_seat_].is_dealer = true;
  config_.button_seat = dealer_seat_;
}

uint8_t GameState::FindNextSeated(uint8_t from_seat) const {
  for (int i = 1; i <= config_.max_players; i++) {
    uint8_t seat = (from_seat + i) % config_.max_players;
    if (players_[seat].seat_state == SeatState::SITTING ||
        players_[seat].seat_state == SeatState::PLAYING) {
      return seat;
    }
  }
  return from_seat;
}

void GameState::PostBlinds() {
  // SB = dealer + 1, BB = dealer + 2
  uint8_t sb_seat = FindNextSeated(dealer_seat_);
  uint8_t bb_seat = FindNextSeated(sb_seat);

  // Heads-up: button posts SB
  int n_seated = 0;
  for (const auto& p : players_) {
    if (p.seat_state == SeatState::SITTING || p.seat_state == SeatState::PLAYING) n_seated++;
  }
  if (n_seated == 2) {
    sb_seat = dealer_seat_;
    bb_seat = FindNextSeated(dealer_seat_);
  }

  auto& sb = players_[sb_seat];
  auto& bb = players_[bb_seat];

  sb.is_small_blind = true;
  bb.is_big_blind = true;

  Chips sb_amt = std::min(config_.small_blind, sb.chips);
  Chips bb_amt = std::min(config_.big_blind, bb.chips);

  sb.Bet(sb_amt);
  EmitActionEvent(sb.id, ActionType::POST_SB, sb_amt, sb.name + " posts SB $" + std::to_string(int(sb_amt)));

  bb.Bet(bb_amt);
  EmitActionEvent(bb.id, ActionType::POST_BB, bb_amt, bb.name + " posts BB $" + std::to_string(int(bb_amt)));

  current_bet_ = bb_amt;
}

void GameState::DealHoleCards() {
  // Single cryptographic shuffle for the whole hand. The commitment is
  // published (via GetLastRngProof) and the seed/nonce revealed after,
  // so the entire deal is externally verifiable.
  rng_dealer_.Shuffle();
  last_rng_proof_ = rng_dealer_.GetProof().ToString();

  const int cards_per_player = config_.HoleCardsPerPlayer();
  for (auto& player : players_) {
    if (player.seat_state == SeatState::SITTING) {
      if (cards_per_player == 4) {
        player.hole_cards.SetOmaha(rng_dealer_.DealOne(), rng_dealer_.DealOne(),
                                   rng_dealer_.DealOne(), rng_dealer_.DealOne());
      } else {
        player.hole_cards.Set(rng_dealer_.DealOne(), rng_dealer_.DealOne());
      }
      player.seat_state = SeatState::PLAYING;
      player.acted_this_round = false;

      // Emit dealt event WITHOUT hole cards — only the player's own cards
      // are sent via player-specific channels. This prevents logging/broadcast leakage.
      EmitEvent(GameEvent::CARDS_DEALT, player.name + " dealt cards");
    }
  }

  num_active_ = ActivePlayerCount();
  if (num_active_ < 2) {
    EmitEvent(GameEvent::ERROR, "Less than 2 active players after dealing");
  }
}

void GameState::DealFlop() {
  if (community_.count >= 3) return;
  DealCommunity(3);
}

void GameState::DealTurn() {
  if (community_.count >= 4) return;
  DealCommunity(1);
}

void GameState::DealRiver() {
  if (community_.count >= 5) return;
  DealCommunity(1);
}

void GameState::DealCommunity(int count) {
  for (int i = 0; i < count && community_.count < 5; i++) {
    // Burn one card (standard Hold'em deal), then draw from the same
    // shuffle used for the hole cards — the whole hand is one verifiable deal.
    rng_dealer_.Burn();
    uint8_t card = rng_dealer_.DealOne();
    if (card == 0xFF) break;  // deck exhausted (should not happen)
    community_.Add(card);
  }
}

void GameState::CheckCapEffect() {
  // When a short stack goes all-in for less than the minimum raise,
  // it does not reopen betting for subsequent players.
  // This is informational only - validation handles the logic.
}

bool GameState::IsBettingRoundComplete() {
  int active = ActivePlayerCount();
  if (active <= 0) return true;

  // All active players must have acted and bets must be equal
  bool all_acted = true;
  double ref_bet = -1;

  for (const auto& p : players_) {
    if (!p.IsActive()) continue;
    if (!p.acted_this_round) {
      all_acted = false;
      break;
    }
    if (ref_bet < 0) {
      ref_bet = p.bet_info.current_bet;
    } else if (p.bet_info.current_bet != ref_bet) {  // exact integer comparison
      all_acted = false;
      break;
    }
  }

  if (!all_acted) return false;

  // All acted and bets match → round complete
  return true;
}

void GameState::EndBettingRound() {
  EmitEvent(GameEvent::BETTING_ROUND_END, "Betting round ended");
}

void GameState::ResetStreet() {
  for (auto& p : players_) {
    p.acted_this_round = false;
  }
}

void GameState::DoShowdown() {
  phase_ = GamePhase::SHOWDOWN;
  EmitEvent(GameEvent::SHOWDOWN, "Showdown! Board: " + community_.CardsStr());

  // Collect all non-folded players
  std::vector<PlayerState*> all_playing;
  for (auto& p : players_) {
    if (!p.IsFolded()) {
      all_playing.push_back(&p);
    }
  }

  // Build pots
  auto pots = pot_manager_.BuildPots(all_playing);

  // Hole cards map
  std::map<int32_t, std::vector<uint8_t>> hole_map;
  for (auto& p : players_) {
    if (p.hole_cards.IsDealt()) {
      hole_map[p.id] = p.hole_cards.ToVector();
    }
  }

  // Community vector
  std::vector<uint8_t> comm;
  for (uint8_t i = 0; i < community_.count; i++) comm.push_back(community_.cards[i]);

  // Evaluate each pot
  int pot_num = 0;
  for (auto& pot : pots) {
    pot_num++;
    auto results = ShowdownEvaluator::EvaluatePot(pot.eligible_players, hole_map, comm, pot.amount);

    for (auto& sr : results) {
      for (auto& p : players_) {
        if (p.id == sr.player_id) {
          p.Receive(sr.amount_won);
          EmitEvent(GameEvent::PAYOUT, p.name + " wins $" + std::to_string(sr.amount_won) +
                                           " with " + sr.hand_description + " from pot #" +
                                           std::to_string(pot_num));
        }
      }
    }
  }

  phase_ = GamePhase::HAND_COMPLETE;
  hand_started_ = false;
  EmitEvent(GameEvent::HAND_COMPLETE, "Hand #" + std::to_string(hand_counter_) + " complete");

  // Reset all players to sitting
  for (auto& p : players_) {
    if (p.seat_state != SeatState::EMPTY) {
      p.seat_state = SeatState::SITTING;
    }
    p.hole_cards.Reset();
    p.bet_info.Reset();
    p.acted_this_round = false;
    p.is_dealer = false;
    p.is_small_blind = false;
    p.is_big_blind = false;
  }

  num_active_ = 0;
  num_all_in_ = 0;
  num_folded_ = 0;
  current_bet_ = 0;
  community_.Reset();
  pot_manager_.Reset();
}

void GameState::ResetHand() {
  for (auto& p : players_) {
    p.bet_info.Reset();
    p.acted_this_round = false;
    p.is_dealer = false;
    p.is_small_blind = false;
    p.is_big_blind = false;
    if (p.seat_state == SeatState::PLAYING || p.seat_state == SeatState::FOLDED ||
        p.seat_state == SeatState::ALL_IN) {
      p.seat_state = SeatState::SITTING;
    }
    p.hole_cards.Reset();
  }
  community_.Reset();
  pot_manager_.Reset();
  current_bet_ = 0;
  actions_since_last_bet_ = 0;
  num_active_ = 0;
  num_all_in_ = 0;
  num_folded_ = 0;
}

void GameState::NextActionSeat() {
  for (int i = 1; i <= config_.max_players; i++) {
    uint8_t seat = (action_seat_ + i) % config_.max_players;
    if (players_[seat].IsActive() && !players_[seat].acted_this_round) {
      action_seat_ = seat;
      // Reset timeout for the next player.
      action_deadline_ = std::chrono::steady_clock::now() +
                         std::chrono::seconds(config_.hand_timeout_seconds);
      timeout_player_id_ = players_[seat].id;
      return;
    }
  }
  // Fallback: find any active
  for (int i = 1; i <= config_.max_players; i++) {
    uint8_t seat = (action_seat_ + i) % config_.max_players;
    if (players_[seat].IsActive()) {
      action_seat_ = seat;
      // Reset timeout.
      action_deadline_ = std::chrono::steady_clock::now() +
                         std::chrono::seconds(config_.hand_timeout_seconds);
      timeout_player_id_ = players_[seat].id;
      return;
    }
  }
}

PlayerState* GameState::GetCurrentPlayer() {
  auto& p = players_[action_seat_];
  if (p.IsActive() && !p.acted_this_round) return &p;
  // Try to find anyone who hasn't acted
  for (auto& pl : players_) {
    if (pl.IsActive() && !pl.acted_this_round) return &pl;
  }
  return nullptr;
}

int32_t GameState::GetCurrentPlayerId() const {
  if (action_seat_ < config_.max_players) return players_[action_seat_].id;
  return -1;
}

int GameState::ActivePlayerCount() const {
  int count = 0;
  for (const auto& p : players_) {
    if (p.IsActive()) count++;
  }
  return count;
}

PlayerState* GameState::GetPlayerAtSeat(uint8_t seat) {
  if (seat >= config_.max_players) return nullptr;
  if (players_[seat].seat_state == SeatState::EMPTY) return nullptr;
  return &players_[seat];
}

Chips GameState::GetPot() const {
  Chips total = pot_manager_.TotalPot();
  for (const auto& p : players_) {
    if (p.seat_state == SeatState::EMPTY || p.IsFolded()) continue;
    total += p.bet_info.current_bet;
  }
  return total;
}

bool GameState::IsHandInProgress() const { return hand_started_; }


int GameState::CurrentStreet() const {
  switch (phase_) {
    case GamePhase::PREFLOP_BETTING:
      return 0;
    case GamePhase::FLOP_BETTING:
      return 1;
    case GamePhase::TURN_BETTING:
      return 2;
    case GamePhase::RIVER_BETTING:
      return 3;
    default:
      return 0;
  }
}

void GameState::EmitActionEvent(int32_t player_id, ActionType action, Chips amount,
                                const std::string& msg) {
  if (!event_callback_) return;
  GameEvent ev;
  ev.type = GameEvent::ACTION_TAKEN;
  ev.message = msg;
  ev.player_id = player_id;
  ev.action = action;
  ev.amount = amount;
  ev.street = CurrentStreet();
  ev.pot_after = GetPot();
  ev.has_action = true;
  event_callback_(ev);
}

void GameState::EmitEvent(GameEvent::Type type, const std::string& msg) {
  if (event_callback_) {
    GameEvent ev;
    ev.type = type;
    ev.message = msg;
    event_callback_(ev);
  }
}

std::string GameState::ToString() const {
  std::ostringstream oss;
  oss << "=== " << config_.table_name << " ===\n";
  oss << "Phase: " << GamePhaseName[static_cast<int>(phase_)] << "\n";
  oss << "Pot: $" << GetPot() << " | Bet: $" << current_bet_ << "\n";
  oss << "Board: " << community_.CardsStr() << "\n";
  oss << "Dealer: Seat " << (int)dealer_seat_ + 1 << "\n";
  oss << "Active: " << ActivePlayerCount() << "\n\n";

  for (size_t i = 0; i < players_.size(); i++) {
    const auto& p = players_[i];
    if (p.seat_state != SeatState::EMPTY) {
      oss << "  [Seat " << (i + 1);
      if (p.is_dealer) oss << " BTN";
      if (p.is_small_blind) oss << " SB";
      if (p.is_big_blind) oss << " BB";
      oss << "] " << p.ToString() << "\n";
    }
  }

  return oss.str();
}

}  // namespace poker_engine::game
