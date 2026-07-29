#pragma once
#include "poker_engine/game/action.h"
#include "poker_engine/game/player_state.h"
#include "poker_engine/game/pot_manager.h"

namespace poker_engine::game {

struct ValidationResult {
  bool valid = false;
  std::string error;
  Chips adjusted_amount = 0;

  static ValidationResult OK(Chips amount = 0) { return {true, "", amount}; }
  static ValidationResult Error(const std::string& msg) { return {false, msg, 0}; }
};

struct MinRaiseInfo {
  Chips min_raise_to = 0;
  Chips min_raise_by = 0;
  Chips max_bet = 0;
  bool is_all_in_less = false;
};

class ActionValidator {
 public:
  // last_raise = size of the previous full raise this street (defaults to 0,
  // meaning "no raise yet" → the big blind is the minimum increment, per
  // NLHE rules).
  static ValidationResult Validate(const GameAction& action, const PlayerState& player,
                                   const std::vector<PlayerState*>& all_players, Chips current_bet,
                                   Chips pot, Chips big_blind, Chips ante,
                                   int num_active_players, int num_all_in, int street,
                                   Chips last_raise = 0);

  static MinRaiseInfo CalculateMinRaise(const PlayerState& player, Chips current_bet, Chips pot,
                                        Chips big_blind,
                                        const std::vector<PlayerState*>& all_players, int street,
                                        Chips last_raise = 0);

  static bool IsCapEffectActive(const std::vector<PlayerState*>& all_players, Chips current_bet,
                                Chips big_blind);
};

}  // namespace poker_engine::game
