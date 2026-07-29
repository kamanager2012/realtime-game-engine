#include "poker_engine/persistence/action_log_repository.h"

#include "poker_engine/game/action.h"
#include "poker_engine/persistence/database_manager.h"

namespace poker_engine::persistence {

void SQLiteActionLogRepository::Log(int64_t hand_id, const game::Action& action) {
  db_.Execute(
      "INSERT INTO action_log (hand_id, player_id, round_name, action_type, amount) "
      "VALUES (" +
      std::to_string(hand_id) + ", " + std::to_string(action.player_id) + ", '" +
      std::to_string(static_cast<int>(action.street)) + "', '" +
      std::string(game::ToString(action.type)) + "', " + std::to_string(action.amount) + ")");
}

std::vector<ActionRecordEntry> SQLiteActionLogRepository::GetForHand(int64_t hand_id) {
  std::vector<ActionRecordEntry> result;

  db_.Query(
      "SELECT hand_id, player_id, round_name, action_type, amount, timestamp "
      "FROM action_log WHERE hand_id = " +
          std::to_string(hand_id) + " ORDER BY id ASC",
      [&](const std::vector<std::string>& row) {
        ActionRecordEntry entry;
        entry.hand_id = std::stoll(row[0]);
        entry.player_id = std::stoll(row[1]);
        entry.round_name = row[2];
        entry.action_type = row[3];
        entry.amount = static_cast<int64_t>(std::stod(row[4]));
        entry.timestamp = row[5];
        result.push_back(entry);
        return true;
      });

  return result;
}

}  // namespace poker_engine::persistence
