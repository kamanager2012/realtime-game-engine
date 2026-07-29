#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "poker_engine/game/game_state.h"

namespace poker_engine::game {

struct TableEvent {
  enum Type {
    PLAYER_JOINED,
    PLAYER_LEFT,
    HAND_STARTED,
    HAND_ENDED,
    ACTION,
    PAYOUT,
    SHOWDOWN,
    ERROR,
    STATE_UPDATE
  };
  Type type;
  std::string message;
};

struct TableSettings {
  std::string name = "Main Table";
  std::string game_type = "NLHE";  // NLHE / PLO / LIMIT ...
  int max_players = 9;
  double small_blind = 0.5;
  double big_blind = 1.0;
  double ante = 0.0;
  double min_buy_in = 10.0;
  double max_buy_in = 200.0;
  int seconds_per_action = 30;
  bool allow_sitting_out = true;

  TableConfig ToGameConfig() const {
    TableConfig cfg;
    cfg.table_name = name;
    cfg.max_players = max_players;
    cfg.small_blind = small_blind;
    cfg.big_blind = big_blind;
    cfg.ante = ante;
    cfg.min_buy_in = min_buy_in;
    cfg.max_buy_in = max_buy_in;
    cfg.hand_timeout_seconds = seconds_per_action;
    cfg.button_seat = 0;
    return cfg;
  }
};

class Table {
 public:
  explicit Table(int32_t table_id, const TableSettings& settings);
  explicit Table(const TableConfig& config);

  bool JoinTable(int32_t player_id, const std::string& name, Chips chips);
  bool JoinTable(int32_t player_id, const std::string& name, Chips chips, uint8_t seat);
  bool LeaveTable(int32_t player_id);
  bool SitDown(int32_t player_id);
  bool SitDown(int32_t player_id, uint8_t seat);
  bool StandUp(int32_t player_id);

  bool PlayerAction(int32_t player_id, ActionType action, double amount = 0);
  bool PlayerAction(int32_t player_id, const GameAction& action);
  bool StartHand();

  const GameState& GetGameState() const { return game_state_; }
  GameState& GetGameStateMut() { return game_state_; }
  const TableSettings& GetSettings() const { return settings_; }
  std::string ToString() const;
  std::string GetStateJSON() const;

  using TableCallback = std::function<void(const TableEvent&)>;
  void SetCallback(TableCallback cb) { table_callback_ = cb; }

  int PlayerCount() const;
  int ActivePlayerCount() const { return game_state_.ActivePlayerCount(); }
  double TableStake() const;
  bool IsPlaying() const { return game_state_.IsHandInProgress(); }
  int32_t GetTableId() const { return table_id_; }

 private:
  int32_t table_id_ = 0;
  GameState game_state_;
  TableSettings settings_;
  TableCallback table_callback_;
  void EmitTableEvent(TableEvent::Type type, const std::string& msg);
};

}  // namespace poker_engine::game
