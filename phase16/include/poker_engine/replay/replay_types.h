#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "poker_engine/game/action.h"
#include "poker_engine/game/game_state.h"

namespace poker_engine::replay {

enum class ReplayEventType : uint8_t {
  HandStart = 0,
  BlindsPosted = 1,
  DealHoleCards = 2,
  ActionTaken = 3,
  DealCommunity = 4,
  Showdown = 5,
  PotDistribution = 6,
  HandEnd = 7,
  PlayerJoin = 8,
  PlayerLeave = 9,
};

struct ReplayEvent {
  uint64_t sequence_id;
  ReplayEventType type;
  int64_t hand_id;
  int32_t player_id;
  double timestamp;
  std::string details;

  std::string Serialize() const;
  static std::optional<ReplayEvent> Deserialize(const std::string& json);
};

struct HandSnapshot {
  int64_t hand_id;
  int64_t hand_number;
  double timestamp;

  struct PlayerSnap {
    int32_t player_id;
    int32_t seat_index;
    std::string display_name;
    int64_t chips_at_start;
    int64_t chips_at_end;
    int64_t net_profit;
    std::vector<uint8_t> hole_cards;
    std::string status;
    bool is_winner = false;
    std::vector<std::string> actions;
  };

  std::vector<PlayerSnap> players;
  std::vector<uint8_t> community_cards;
  std::vector<uint8_t> pot_distribution;
  std::string phase;
  int64_t total_pot;
  std::vector<int64_t> winners;
};

struct ReplayConfig {
  double playback_speed = 1.0;
  bool show_hole_cards = true;
  bool auto_play = false;
  int max_events_per_batch = 50;
  double event_delay_ms = 500;
};

struct ReplayQuery {
  std::optional<int64_t> player_id;
  std::optional<int64_t> table_id;
  std::optional<int64_t> hand_id;
  int64_t start_time = 0;
  int64_t end_time = 0;
  int limit = 100;
  int offset = 0;
  bool include_details = true;

  std::string ToSQLWhere() const;
};

}  // namespace poker_engine::replay
