#include "poker_engine/persistence/db_writer.h"

#include "poker_engine/base/logging.h"
#include "poker_engine/game/action.h"

namespace poker_engine::persistence {

AsyncDbWriter::AsyncDbWriter(DatabaseManager& db, std::shared_ptr<SQLiteHandRepository> hand_repo,
                             std::shared_ptr<PlayerStatsRepository> stats_repo)
    : db_(db), hand_repo_(std::move(hand_repo)), stats_repo_(std::move(stats_repo)) {
  writer_.SetBatchThreshold(32);
  writer_.SetMaxDelayMs(50);
}

AsyncDbWriter::~AsyncDbWriter() { Stop(); }

void AsyncDbWriter::Start() {
  writer_.Start();
  LOG_INFO("Async DB writer started");
}

void AsyncDbWriter::Stop() {
  writer_.Stop();
  LOG_INFO("Async DB writer stopped");
}

void AsyncDbWriter::SaveHandAsync(const game::GameState& final_state) {
  auto state_copy = std::make_shared<game::GameState>(final_state);

  writer_.Enqueue([this, state_copy]() {
    try {
      hand_repo_->SaveHand(*state_copy);
      LOG_DEBUG("Async hand saved: table={} hand={}", state_copy->table_id,
                state_copy->hand_number);
    } catch (const std::exception& e) {
      LOG_ERROR("Async hand save failed: {}", e.what());
    }
  });
}

void AsyncDbWriter::UpdatePlayerStatsAsync(int64_t player_id, bool won, int64_t profit) {
  writer_.Enqueue([this, player_id, won, profit]() {
    try {
      stats_repo_->UpdateFromHand(player_id, won, profit, true, false);
    } catch (const std::exception& e) {
      LOG_ERROR("Async stats update failed: {}", e.what());
    }
  });
}

void AsyncDbWriter::LogActionsAsync(int64_t hand_id, const std::vector<game::Action>& actions) {
  auto actions_copy = std::make_shared<std::vector<game::Action>>(actions);

  writer_.Enqueue([this, hand_id, actions_copy]() {
    try {
      for (auto& action : *actions_copy) {
        // Action logging — direct execute
        (void)hand_id;
        (void)action;
      }
    } catch (const std::exception& e) {
      LOG_ERROR("Async action log failed: {}", e.what());
    }
  });
}

size_t AsyncDbWriter::PendingTasks() const { return writer_.PendingCount(); }

}  // namespace poker_engine::persistence
