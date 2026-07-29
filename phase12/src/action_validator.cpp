#include "poker_engine/game/action_validator.h"

#include <algorithm>
#include <cmath>

namespace poker_engine::game {

ValidationResult ActionValidator::Validate(const GameAction& action, const PlayerState& player,
                                           const std::vector<PlayerState*>& all_players,
                                           Chips current_bet, Chips pot, Chips big_blind,
                                           Chips ante, int num_active_players, int num_all_in,
                                           int street) {
  // Player must be active to act
  if (!player.IsActive() && !player.IsAllIn()) {
    return ValidationResult::Error("Player is not active");
  }
  if (player.IsAllIn()) {
    return ValidationResult::Error("Player already all-in");
  }
  if (player.IsFolded()) {
    return ValidationResult::Error("Player already folded");
  }

  // Reject negative amounts — critical for financial integrity.
  if (action.amount < 0) {
    return ValidationResult::Error("Invalid negative amount");
  }

  Chips player_current = player.bet_info.current_bet;
  Chips to_call = current_bet - player_current;
  if (to_call < 0) to_call = 0;

  switch (action.type) {
    case ActionType::FOLD:
      return ValidationResult::OK(0);

    case ActionType::CHECK:
      if (to_call > 0) {
        return ValidationResult::Error("Cannot check: need to call $" +
                                       std::to_string(int(to_call)));
      }
      return ValidationResult::OK(0);

    case ActionType::CALL: {
      if (to_call <= 0) {
        return ValidationResult::Error("Nothing to call; check instead");
      }
      double call_amount = std::min(to_call, player.chips);
      return ValidationResult::OK(call_amount);
    }

    case ActionType::BET: {
      if (current_bet > 0) {  // exact integer comparison (Chips = int64_t)
        return ValidationResult::Error("Cannot bet: there is already $" +
                                       std::to_string(int(current_bet)) +
                                       " to call; raise instead");
      }
      Chips min_bet = big_blind;
      if (action.amount < min_bet && player.chips > min_bet) {
        return ValidationResult::Error("Min bet is $" + std::to_string(int(min_bet)));
      }
      Chips actual = std::min(action.amount, player.chips);
      return ValidationResult::OK(actual);
    }

    case ActionType::RAISE: {
      if (action.amount <= current_bet) {  // exact integer comparison
        return ValidationResult::Error("Raise must exceed current bet $" +
                                       std::to_string(int(current_bet)));
      }

      Chips min_raise_to = current_bet + big_blind;
      Chips all_in_to = player_current + player.chips;

      // If player can't afford min raise, allow all-in
      if (action.amount < min_raise_to && all_in_to >= min_raise_to) {
        return ValidationResult::Error("Min raise to $" + std::to_string(int(min_raise_to)));
      }

      Chips actual = std::min(action.amount, all_in_to);
      Chips needed = actual - player_current;
      if (needed < 0) needed = 0;
      return ValidationResult::OK(needed);
    }

    case ActionType::ALL_IN: {
      Chips all_in_amount = player.chips;
      if (all_in_amount <= 0) {
        return ValidationResult::Error("No chips to go all-in");
      }
      return ValidationResult::OK(all_in_amount);
    }

    default:
      return ValidationResult::Error("Invalid action type");
  }
}

MinRaiseInfo ActionValidator::CalculateMinRaise(const PlayerState& player, Chips current_bet,
                                                Chips pot, Chips big_blind,
                                                const std::vector<PlayerState*>& all_players,
                                                int street) {
  MinRaiseInfo info;
  info.min_raise_to = current_bet + big_blind;
  info.min_raise_by = info.min_raise_to - player.bet_info.current_bet;
  info.max_bet = player.chips + player.bet_info.current_bet;
  info.is_all_in_less = (info.max_bet < info.min_raise_to);
  return info;
}

bool ActionValidator::IsCapEffectActive(const std::vector<PlayerState*>& all_players,
                                        Chips current_bet, Chips big_blind) {
  for (auto* p : all_players) {
    if (p->IsAllIn() && p->bet_info.current_bet < current_bet + big_blind) return true;
  }
  return false;
}

}  // namespace poker_engine::game
