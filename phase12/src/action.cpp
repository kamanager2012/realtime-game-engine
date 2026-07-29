#include "poker_engine/game/action.h"

#include <cmath>
#include <sstream>

namespace poker_engine::game {

namespace {
const char* ActionTypeNames[] = {"Fold",   "Check",  "Call",     "Bet",     "Raise",   "AllIn",
                                 "PostSB", "PostBB", "PostAnte", "SitDown", "StandUp", "SitOut"};

const char* RankLabels[] = {"2", "3", "4", "5", "6", "7", "8", "9", "T", "J", "Q", "K", "A"};
const char* SuitLabels[] = {"c", "h", "d", "s"};

std::string CardIdToString(uint8_t id) {
  if (id >= 52) return "??";
  return std::string(RankLabels[id >> 2]) + SuitLabels[id & 3];
}
}  // namespace

std::string GameAction::ToString() const {
  std::ostringstream oss;
  int idx = static_cast<int>(type);
  if (idx >= 0 && idx < 12) {
    oss << ActionTypeNames[idx];
  } else {
    oss << "Unknown(" << idx << ")";
  }
  if (amount > 0) oss << " " << amount;
  oss << " [p" << player_id << " street=" << street << "]";
  return oss.str();
}

bool GameAction::IsValid() const {
  switch (type) {
    case ActionType::FOLD:
    case ActionType::CHECK:
    case ActionType::SIT_DOWN:
    case ActionType::STAND_UP:
    case ActionType::SIT_OUT:
      return amount == 0;
    case ActionType::CALL:
    case ActionType::BET:
    case ActionType::RAISE:
    case ActionType::ALL_IN:
    case ActionType::POST_SB:
    case ActionType::POST_BB:
    case ActionType::POST_ANTE:
      return amount >= 0 && !std::isnan(amount) && !std::isinf(amount);
    default:
      return false;
  }
}

std::string HoleCards::ToString() const {
  if (!dealt) return count == 4 ? "[?? ?? ?? ??]" : "[?? ??]";
  std::string result = "[";
  for (int i = 0; i < count; ++i) {
    if (i > 0) result += " ";
    result += CardIdToString(cards[i]);
  }
  result += "]";
  return result;
}

std::string CommunityCards::ToString() const {
  if (count == 0) return "[]";
  std::string result = "[";
  for (uint8_t i = 0; i < count; ++i) {
    if (i > 0) result += " ";
    result += CardIdToString(cards[i]);
  }
  result += "]";
  return result;
}

std::string CommunityCards::CardsStr() const { return ToString(); }

std::string WinResult::ToString() const {
  std::ostringstream oss;
  oss << "Player " << player_id << " wins " << amount;
  if (!hand_name.empty()) oss << " with " << hand_name;
  if (hand_rank > 0) oss << " (rank=" << hand_rank << ")";
  return oss.str();
}

}  // namespace poker_engine::game
