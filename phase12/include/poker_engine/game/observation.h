#pragma once
#include <cstdint>
#include <vector>

#include "poker_engine/game/action.h"
#include "poker_engine/game/player_state.h"

namespace poker_engine::game {

// Redacted, per-player view of a single opponent seat. Deliberately carries NO
// hole_cards field: an agent literally cannot read opponents' cards through the
// type system. `has_cards` still tells you whether the seat was dealt in, which
// is public information at the table.
struct PlayerView {
  int32_t id = 0;
  uint8_t seat = 255;
  SeatState seat_state = SeatState::EMPTY;
  Chips chips = 0;
  BetInfo bet_info;
  bool is_dealer = false;
  bool is_small_blind = false;
  bool is_big_blind = false;
  bool acted_this_round = false;
  bool has_cards = false;

  bool IsPlaying() const {
    return seat_state == SeatState::PLAYING || seat_state == SeatState::ALL_IN;
  }
  bool IsActive() const { return seat_state == SeatState::PLAYING; }
  bool IsAllIn() const { return seat_state == SeatState::ALL_IN; }
  bool IsFolded() const { return seat_state == SeatState::FOLDED; }
};

// A redacted observation handed to an agent's Decide(). It contains public table
// state plus ONLY the viewer's own hole cards. Opponents appear as PlayerView
// (no hole_cards), so the agent has no way to peek at hidden information.
//
// This is the imperfect-information boundary of the environment: it is what a
// real player would legally see. GameState::ObserveFor(viewer_id) builds it.
struct Observation {
  GamePhase phase = GamePhase::WAITING;
  Chips current_bet = 0;
  Chips pot = 0;
  Chips big_blind = 0;
  Chips ante = 0;
  CommunityCards community;

  int32_t viewer_id = -1;
  HoleCards my_hole_cards;
  std::vector<PlayerView> players;

  // Convenience accessors for the viewer's own seat.
  const PlayerView* Me() const {
    for (const auto& p : players) {
      if (p.id == viewer_id) return &p;
    }
    return nullptr;
  }
  const HoleCards& MyHoleCards() const { return my_hole_cards; }
  Chips MyChips() const {
    const PlayerView* me = Me();
    return me ? me->chips : 0;
  }
  Chips MyCurrentBet() const {
    const PlayerView* me = Me();
    return me ? me->bet_info.current_bet : 0;
  }
};

}  // namespace poker_engine::game
