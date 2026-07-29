#include "poker_engine/game/table.h"

#include <algorithm>
#include <sstream>

namespace poker_engine::game {

Table::Table(int32_t table_id, const TableSettings& settings)
    : table_id_(table_id), game_state_(settings.ToGameConfig()), settings_(settings) {}

Table::Table(const TableConfig& config) : table_id_(0), game_state_(config) {
  settings_.name = config.table_name;
  settings_.max_players = config.max_players;
  settings_.small_blind = config.small_blind;
  settings_.big_blind = config.big_blind;
}

bool Table::JoinTable(int32_t player_id, const std::string& name, Chips chips) {
  bool ok = game_state_.AddPlayer(player_id, name, chips);
  if (ok) {
    EmitTableEvent(TableEvent::PLAYER_JOINED, name + " joined");
  }
  return ok;
}

bool Table::JoinTable(int32_t player_id, const std::string& name, Chips chips, uint8_t seat) {
  bool ok = game_state_.AddPlayerAtSeat(player_id, name, chips, seat);
  if (ok) {
    EmitTableEvent(TableEvent::PLAYER_JOINED, name + " joined");
  }
  return ok;
}

bool Table::LeaveTable(int32_t player_id) {
  std::string name;
  for (const auto& p : game_state_.AllPlayers()) {
    if (p.id == player_id) {
      name = p.name;
      break;
    }
  }
  bool ok = game_state_.RemovePlayer(player_id);
  if (ok) EmitTableEvent(TableEvent::PLAYER_LEFT, name + " left");
  return ok;
}

bool Table::SitDown(int32_t player_id) {
  // Player must be at table already
  for (const auto& p : game_state_.AllPlayers()) {
    if (p.id == player_id) return true;
  }
  return false;
}

bool Table::SitDown(int32_t player_id, uint8_t seat) {
  return game_state_.SitDown(player_id, seat);
}

bool Table::StandUp(int32_t player_id) { return game_state_.StandUp(player_id); }

bool Table::PlayerAction(int32_t player_id, ActionType action, double amount) {
  GameAction ga;
  ga.type = action;
  ga.amount = amount;
  ga.player_id = player_id;
  return PlayerAction(player_id, ga);
}

bool Table::PlayerAction(int32_t player_id, const GameAction& action) {
  bool ok = game_state_.ProcessAction(player_id, action);
  if (ok) {
    EmitTableEvent(TableEvent::ACTION, "P" + std::to_string(player_id) + ": " + action.ToString());
  } else {
    EmitTableEvent(TableEvent::ERROR, "Invalid action by P" + std::to_string(player_id));
  }
  return ok;
}

bool Table::StartHand() {
  bool ok = game_state_.StartHand();
  if (ok) {
    EmitTableEvent(TableEvent::HAND_STARTED, "New hand on " + settings_.name);
  }
  return ok;
}

int Table::PlayerCount() const { return static_cast<int>(game_state_.AllPlayers().size()); }

double Table::TableStake() const { return settings_.big_blind; }

std::string Table::ToString() const {
  std::ostringstream oss;
  oss << "Table{" << settings_.name << " players=" << PlayerCount() << " stake=$"
      << int(TableStake()) << " phase=" << GamePhaseName[static_cast<int>(game_state_.GetPhase())]
      << "}";
  return oss.str();
}

std::string Table::GetStateJSON() const {
  std::ostringstream oss;
  oss << "{\"table_id\":" << table_id_ << ",\"name\":\"" << settings_.name << "\""
      << ",\"pot\":" << int(game_state_.GetPot()) << ",\"bet\":" << int(game_state_.GetCurrentBet())
      << ",\"phase\":\"" << GamePhaseName[static_cast<int>(game_state_.GetPhase())] << "\""
      << ",\"players\":[";

  bool first = true;
  for (const auto& p : game_state_.AllPlayers()) {
    if (p.seat_state == SeatState::EMPTY) continue;
    if (!first) oss << ",";
    first = false;
    oss << "{\"id\":" << p.id << ",\"name\":\"" << p.name << "\""
        << ",\"chips\":" << int(p.chips) << ",\"bet\":" << int(p.bet_info.current_bet)
        << ",\"state\":\"" << static_cast<int>(p.seat_state) << "\"";
    if (p.is_dealer) oss << ",\"btn\":true";
    if (p.is_small_blind) oss << ",\"sb\":true";
    if (p.is_big_blind) oss << ",\"bb\":true";
    oss << "}";
  }
  oss << "]}";
  return oss.str();
}

void Table::EmitTableEvent(TableEvent::Type type, const std::string& msg) {
  if (table_callback_) {
    TableEvent ev;
    ev.type = type;
    ev.message = msg;
    table_callback_(ev);
  }
}

}  // namespace poker_engine::game
