#include "poker_engine/game/player_state.h"

#include <algorithm>
#include <sstream>

namespace poker_engine::game {

Chips PlayerState::Bet(Chips amount) {
  Chips actual = std::min(amount, chips);
  chips -= actual;
  bet_info.current_bet += actual;
  bet_info.total_invested += actual;
  if (chips == 0 && actual > 0) {
    seat_state = SeatState::ALL_IN;
  }
  return actual;
}

Chips PlayerState::Receive(Chips amount) {
  Chips actual = std::max(Chips{0}, amount);
  chips += actual;
  return actual;
}

std::string PlayerState::ToString() const {
  std::ostringstream oss;
  oss << "Player{id=" << id << " name=" << name << " chips=" << chips
      << " seat=" << static_cast<int>(seat);

  const char* state_names[] = {"Empty", "Sitting", "Playing", "Folded", "AllIn", "SittingOut"};
  int si = static_cast<int>(seat_state);
  oss << " state=" << (si >= 0 && si < 6 ? state_names[si] : "???");

  if (hole_cards.IsDealt()) oss << " " << hole_cards.ToString();
  if (bet_info.current_bet > 0) oss << " bet=" << bet_info.current_bet;
  if (bet_info.total_invested > 0) oss << " invested=" << bet_info.total_invested;
  if (is_dealer) oss << " [D]";
  if (is_small_blind) oss << " [SB]";
  if (is_big_blind) oss << " [BB]";
  if (is_hero) oss << " [Hero]";
  oss << "}";
  return oss.str();
}

}  // namespace poker_engine::game
