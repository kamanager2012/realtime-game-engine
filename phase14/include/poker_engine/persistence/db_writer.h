#pragma once

#include <memory>

#include "poker_engine/game/action.h"
#include "poker_engine/persistence/async_writer.h"
#include "poker_engine/persistence/database_manager.h"
#include "poker_engine/persistence/hand_repository.h"
#include "poker_engine/persistence/player_stats_repository.h"

namespace poker_engine::persistence {

class AsyncDbWriter {
 public:
  AsyncDbWriter(DatabaseManager& db, std::shared_ptr<SQLiteHandRepository> hand_repo,
                std::shared_ptr<PlayerStatsRepository> stats_repo);
  ~AsyncDbWriter();

  void Start();
  void Stop();

  void SaveHandAsync(const game::GameState& final_state);
  void UpdatePlayerStatsAsync(int64_t player_id, bool won, int64_t profit);
  void LogActionsAsync(int64_t hand_id, const std::vector<game::Action>& actions);

  size_t PendingTasks() const;

 private:
  DatabaseManager& db_;
  std::shared_ptr<SQLiteHandRepository> hand_repo_;
  std::shared_ptr<PlayerStatsRepository> stats_repo_;
  AsyncWriter writer_;
};

}  // namespace poker_engine::persistence
