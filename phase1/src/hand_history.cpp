#include "poker_engine/phase1/hand_history.h"

#include <iomanip>
#include <sstream>

namespace poker_engine {
namespace phase1 {

std::string ActionTypeToString(ActionType a) {
  switch (a) {
    case ActionType::FOLD:
      return "folds";
    case ActionType::CHECK:
      return "checks";
    case ActionType::CALL:
      return "calls";
    case ActionType::BET:
      return "bets";
    case ActionType::RAISE:
      return "raises";
    case ActionType::ALL_IN:
      return "all-in";
    case ActionType::POST_SB:
      return "posts SB";
    case ActionType::POST_BB:
      return "posts BB";
    case ActionType::COLLECT:
      return "collects";
    case ActionType::SHOW_WIN:
      return "shows (wins)";
    case ActionType::SHOW_TIE:
      return "shows (ties)";
    case ActionType::UNCALLED_BET_RETURN:
      return "uncalled bet returned";
    default:
      return "unknown";
  }
}

std::string PlayerAction::ToString() const {
  std::ostringstream oss;
  oss << player_name << ": ";
  switch (action) {
    case ActionType::BET:
      oss << "bets $" << std::fixed << std::setprecision(2) << amount;
      break;
    case ActionType::RAISE:
      oss << "raises to $" << std::fixed << std::setprecision(2) << amount;
      break;
    case ActionType::CALL:
      oss << "calls $" << std::fixed << std::setprecision(2) << amount;
      break;
    case ActionType::FOLD:
      oss << "folds";
      break;
    case ActionType::CHECK:
      oss << "checks";
      break;
    case ActionType::ALL_IN:
      oss << "all-in $" << std::fixed << std::setprecision(2) << amount;
      break;
    case ActionType::POST_SB:
      oss << "posts small blind $" << std::fixed << std::setprecision(2) << amount;
      break;
    case ActionType::POST_BB:
      oss << "posts big blind $" << std::fixed << std::setprecision(2) << amount;
      break;
    case ActionType::COLLECT:
      oss << "collected $" << std::fixed << std::setprecision(2) << amount;
      break;
    default:
      oss << ActionTypeToString(action);
  }
  return oss.str();
}

// Hero 的某街行动
std::optional<PlayerAction> HandHistory::HeroActionOnStreet(Street s) const {
  if (streets.size() <= static_cast<size_t>(s)) return std::nullopt;
  const auto& actions = streets[static_cast<size_t>(s)].actions;
  for (const auto& a : actions) {
    if (a.player_name == hero_name) return a;
  }
  return std::nullopt;
}

double HandHistory::HeroTotalInvested() const {
  double total = 0;
  for (const auto& s : streets) {
    for (const auto& a : s.actions) {
      if (a.player_name == hero_name &&
          (a.action == ActionType::CALL || a.action == ActionType::BET ||
           a.action == ActionType::RAISE || a.action == ActionType::ALL_IN ||
           a.action == ActionType::POST_SB || a.action == ActionType::POST_BB))
        total += a.amount;
    }
  }
  return total;
}

double HandHistory::HeroNetProfit() const {
  double won = 0;
  for (const auto& p : pots) {
    for (const auto& w : p.winners) {
      if (w == hero_name) won += p.amount;
    }
  }
  return won - HeroTotalInvested();
}

std::string HandHistory::Summary() const {
  std::ostringstream oss;
  oss << "Hand #" << hand_id;
  if (!site.empty()) oss << " [" << site << "]";
  oss << " " << game_type << "\n";
  oss << "Table: " << table_name << " (" << max_seats << "-max)\n";
  oss << "Blinds: $" << small_blind << "/$" << big_blind << "\n";
  if (!hero_name.empty()) {
    oss << "Hero: " << hero_name;
    if (hero_cards_known_) {
      oss << " [" << hero_cards[0].ToString() << " " << hero_cards[1].ToString() << "]";
    }
    oss << "\n";
  }
  oss << "Board: ";
  for (size_t i = 0; i < board.size(); i++) {
    if (i == 3) oss << " |";
    if (i == 4) oss << " |";
    oss << " " << board[i].ToString();
  }
  oss << "\nPot: $" << std::fixed << std::setprecision(2) << total_pot << "\n";
  if (!pots.empty()) {
    for (size_t i = 0; i < pots.size(); i++) {
      oss << "  Pot " << (i + 1) << ": $" << pots[i].amount << " winner(s): ";
      for (size_t j = 0; j < pots[i].winners.size(); j++) {
        if (j > 0) oss << ", ";
        oss << pots[i].winners[j];
      }
      oss << "\n";
    }
  }

  // 每个街的行动
  const char* street_names[] = {"PREFLOP", "FLOP", "TURN", "RIVER"};
  for (size_t si = 0; si < streets.size(); si++) {
    const auto& sr = streets[si];
    oss << "\n--- " << street_names[si];
    if (!sr.community_cards.empty()) {
      oss << " [";
      for (size_t ci = 0; ci < sr.community_cards.size(); ci++) {
        if (ci > 0) oss << " ";
        oss << sr.community_cards[ci].ToString();
      }
      oss << "]";
    }
    oss << " (pot: $" << sr.pot_before << ") ---\n";
    for (const auto& a : sr.actions) {
      oss << "  " << a.ToString() << "\n";
    }
  }

  return oss.str();
}

}  // namespace phase1
}  // namespace poker_engine
