#pragma once

#include <string>
#include <vector>

#include "poker_engine/game/action.h"

namespace poker_engine::persistence {

class DatabaseManager;  // forward declaration

struct ActionRecordEntry {
  int64_t hand_id;
  int64_t player_id;
  std::string round_name;
  std::string action_type;
  int64_t amount;
  std::string timestamp;
};

class IActionLogRepository {
 public:
  virtual ~IActionLogRepository() = default;
  virtual void Log(int64_t hand_id, const game::Action& action) = 0;
  virtual std::vector<ActionRecordEntry> GetForHand(int64_t hand_id) = 0;
};

class SQLiteActionLogRepository : public IActionLogRepository {
 public:
  explicit SQLiteActionLogRepository(DatabaseManager& db) : db_(db) {}

  void Log(int64_t hand_id, const game::Action& action) override;
  std::vector<ActionRecordEntry> GetForHand(int64_t hand_id) override;

 private:
  DatabaseManager& db_;
};

}  // namespace poker_engine::persistence
