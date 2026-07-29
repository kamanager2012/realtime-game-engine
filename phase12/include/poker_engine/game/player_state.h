#pragma once
#include <cstdint>
#include <string>

#include "poker_engine/game/action.h"

namespace poker_engine::game {

struct BetInfo {
  Chips current_bet = 0;
  Chips total_invested = 0;
  Chips side_pot_eligible = 0;
  void Reset() {
    current_bet = 0;
    total_invested = 0;
    side_pot_eligible = 0;
  }
};

struct PlayerState {
  int32_t id = 0;
  std::string name;
  Chips chips = 0;
  uint8_t seat = 255;
  SeatState seat_state = SeatState::EMPTY;

  HoleCards hole_cards;
  BetInfo bet_info;
  bool acted_this_round = false;
  bool is_dealer = false;
  bool is_small_blind = false;
  bool is_big_blind = false;
  bool is_hero = false;

  bool IsPlaying() const {
    return seat_state == SeatState::PLAYING || seat_state == SeatState::ALL_IN;
  }
  bool IsActive() const { return seat_state == SeatState::PLAYING; }
  bool IsAllIn() const { return seat_state == SeatState::ALL_IN; }
  bool IsFolded() const { return seat_state == SeatState::FOLDED; }
  bool IsSitting() const {
    return seat_state == SeatState::SITTING || seat_state == SeatState::SITTING_OUT;
  }
  bool HasCards() const { return hole_cards.IsDealt(); }

  Chips Bet(Chips amount);
  Chips Receive(Chips amount);
  bool CanAfford(Chips amount) const { return chips >= amount; }
  std::string ToString() const;
};

}  // namespace poker_engine::game
