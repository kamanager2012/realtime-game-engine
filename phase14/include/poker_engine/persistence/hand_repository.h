#pragma once

#include <optional>
#include <string>
#include <vector>

#include "poker_engine/evaluator/card.h"
#include "poker_engine/game/action.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/game/player_state.h"

namespace poker_engine::persistence {

class DatabaseManager;  // forward declaration

struct HandRecord {
  int64_t hand_id;
  std::string table_id;
  uint32_t hand_number;
  std::string phase;
  std::string community_cards;  // JSON
  int64_t pot_amount;
  std::string winners_json;  // JSON
  std::string timestamp;
};

struct HandActionRecord {
  int64_t hand_id;
  int64_t player_id;
  std::string round_name;
  std::string action_type;
  int64_t amount;
  std::string timestamp;
};

class IHandRepository {
 public:
  virtual ~IHandRepository() = default;

  // 保存一手牌的完整历史
  virtual int64_t SaveHand(const game::GameState& final_state) = 0;

  // 记录行动
  virtual void SaveAction(int64_t hand_id, const game::GameAction& action) = 0;

  // 查询
  virtual std::optional<HandRecord> GetHand(int64_t hand_id) = 0;
  virtual std::vector<HandRecord> GetPlayerHands(int64_t player_id, int limit) = 0;
  virtual std::vector<HandRecord> GetTableHands(const std::string& table_id, int limit) = 0;
};

class SQLiteHandRepository : public IHandRepository {
 public:
  explicit SQLiteHandRepository(DatabaseManager& db) : db_(db) {}

  int64_t SaveHand(const game::GameState& final_state) override;
  void SaveAction(int64_t hand_id, const game::GameAction& action) override;
  std::optional<HandRecord> GetHand(int64_t hand_id) override;
  std::vector<HandRecord> GetPlayerHands(int64_t player_id, int limit) override;
  std::vector<HandRecord> GetTableHands(const std::string& table_id, int limit) override;

 private:
  DatabaseManager& db_;

  // JSON 序列化辅助
  std::string ActionsToJson(const std::vector<game::GameAction>& actions) const;
  std::string WinnersToJson(const std::vector<int32_t>& winners) const;
  std::string CardsToJson(const std::vector<poker_engine::Card>& cards) const;
};

}  // namespace poker_engine::persistence
