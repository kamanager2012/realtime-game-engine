#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include "poker_engine/game/game_state.h"
#include "poker_engine/persistence/database_manager.h"
#include "poker_engine/persistence/hand_repository.h"
#include "replay_types.h"

namespace poker_engine::replay {

class ReplayObserver {
 public:
  virtual ~ReplayObserver() = default;
  virtual void OnEvent(const ReplayEvent& event) = 0;
  virtual void OnSnapshot(const HandSnapshot& snapshot) = 0;
  virtual void OnComplete(int64_t hand_id) = 0;
  virtual void OnError(const std::string& error) = 0;
};

class ReplayEngine {
 public:
  explicit ReplayEngine(persistence::DatabaseManager& db);
  ~ReplayEngine();

  std::vector<int64_t> ListHands(const ReplayQuery& query);
  std::optional<HandSnapshot> GetHandSummary(int64_t hand_id);

  bool StartReplay(int64_t hand_id, ReplayObserver* observer,
                   const ReplayConfig& config = ReplayConfig());
  void Pause();
  void Resume();
  void Seek(double seconds);
  void Stop();

  bool IsPlaying() const { return is_playing_; }
  int64_t CurrentHandId() const { return current_hand_id_; }
  double CurrentTime() const { return current_time_; }

 private:
  bool LoadHandData(int64_t hand_id);
  void BuildEventTimeline();
  void ReplayLoop();
  void ProcessEvent(const ReplayEvent& event);
  void RebuildState(const ReplayEvent& event);
  void PushSnapshot();

  persistence::DatabaseManager& db_;
  persistence::SQLiteHandRepository* hand_repo_ = nullptr;

  ReplayConfig config_;
  ReplayObserver* observer_ = nullptr;

  int64_t current_hand_id_ = -1;
  std::vector<ReplayEvent> event_timeline_;
  HandSnapshot snapshot_;

  std::atomic<bool> is_playing_{false};
  std::atomic<bool> is_paused_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<double> current_time_{0.0};

  std::thread replay_thread_;
  std::mutex state_mutex_;

  size_t current_event_index_ = 0;

  std::unordered_map<int32_t, int64_t> player_investments_;
  std::unordered_map<int32_t, int64_t> player_chips_;
  std::vector<uint8_t> community_cards_;
  game::GamePhase current_phase_ = game::GamePhase::WAITING;
  int64_t current_bet_ = 0;
  uint8_t dealer_seat_ = 0;
  int32_t current_player_ = -1;
};

}  // namespace poker_engine::replay
