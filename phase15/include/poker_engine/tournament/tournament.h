#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "poker_engine/game/game_state.h"

namespace poker_engine::tournament {

enum class TournamentType : uint8_t {
  Freezeout = 0,
  Rebuy = 1,
  Reentry = 2,
  Bounty = 3,
  Satellite = 4,
  _COUNT = 5
};

inline const char* TournamentTypeName(TournamentType t) {
  static constexpr const char* names[] = {"Freezeout", "Rebuy", "Reentry", "Bounty", "Satellite"};
  return names[static_cast<int>(t)];
}

enum class TournamentStatus : uint8_t {
  Registration = 0,
  Running = 1,
  LateRegistration = 2,
  OnBreak = 3,
  FinalTable = 4,
  Completed = 5,
  Cancelled = 6,
  _COUNT = 7
};

inline const char* TournamentStatusName(TournamentStatus s) {
  static constexpr const char* names[] = {"Registration", "Running",    "LateRegistration",
                                          "OnBreak",      "FinalTable", "Completed",
                                          "Cancelled"};
  return names[static_cast<int>(s)];
}

struct BlindLevel {
  int level = 0;
  double small_blind = 10.0;
  double big_blind = 20.0;
  double ante = 0.0;
  int duration_minutes = 15;

  std::string ToString() const;
};

struct TournamentConfig {
  std::string name = "Tournament";
  TournamentType type = TournamentType::Freezeout;

  int starting_stack = 1500;
  int max_players = 100;
  int players_per_table = 9;
  double buy_in = 10.0;
  double entry_fee = 1.0;
  double guaranteed_prize_pool = 0.0;

  bool has_rebuys = false;
  int max_rebuys = 0;
  double rebuy_cost = 10.0;
  double rebuy_stack = 1500;
  int rebuy_through_level = 6;

  bool has_addon = false;
  double addon_cost = 10.0;
  double addon_stack = 3000;

  bool has_bounty = false;
  double bounty_amount = 5.0;

  int late_registration_minutes = 60;

  std::vector<BlindLevel> blind_schedule;
  std::vector<double> payout_percentages = {50, 30, 20};

  static std::vector<BlindLevel> GenerateTurboBlinds(double starting_sb = 10.0,
                                                     int num_levels = 20);
  static std::vector<BlindLevel> GenerateDeepStackBlinds(double starting_sb = 5.0,
                                                         int num_levels = 25);

  std::string ToString() const;
};

struct TournamentPlayer {
  int id = 0;
  std::string name;
  double chips = 0.0;
  int table_id = -1;
  int seat = -1;
  int starting_stack = 0;

  bool active = true;
  bool sitting_out = false;
  bool eliminated = false;

  int rebuys_used = 0;
  bool addon_used = false;
  double bounty_on_head = 0.0;
  double bounties_won = 0.0;

  int hands_played = 0;
  double total_won = 0.0;
  double total_invested = 0.0;
  int finish_position = 0;

  bool IsInGame() const { return active && !sitting_out && chips > 0; }
  double StackInBB(double big_blind) const { return big_blind > 0 ? chips / big_blind : 0; }

  std::string ToString() const;
};

struct TournamentTable {
  int id = 0;
  std::vector<int> player_ids;
  int max_seats = 9;
  int hand_count = 0;
  double total_pot = 0.0;

  bool IsFull() const { return static_cast<int>(player_ids.size()) >= max_seats; }
  int ActivePlayers() const;

  std::string ToString() const;
};

struct TournamentEvent {
  enum class Type : uint8_t {
    PlayerRegistered = 0,
    PlayerEliminated = 1,
    PlayerRebuy = 2,
    PlayerAddon = 3,
    BlindLevelUp = 4,
    TableRebalance = 5,
    HandCompleted = 6,
    TournamentStart = 7,
    TournamentEnd = 8,
    BreakStart = 9,
    BreakEnd = 10,
    _COUNT = 11
  };

  Type type = Type::PlayerRegistered;
  int player_id = 0;
  int hand_id = 0;
  int level = 0;
  double amount = 0.0;
  std::string detail;
  std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();

  std::string ToString() const;
};

inline const char* TournamentEventTypeName(TournamentEvent::Type t) {
  static constexpr const char* names[] = {"PlayerRegistered", "PlayerEliminated", "PlayerRebuy",
                                          "PlayerAddon",      "BlindLevelUp",     "TableRebalance",
                                          "HandCompleted",    "TournamentStart",  "TournamentEnd",
                                          "BreakStart",       "BreakEnd"};
  return names[static_cast<int>(t)];
}

class TournamentManager {
 public:
  explicit TournamentManager(const TournamentConfig& config);

  bool RegisterPlayer(int player_id, const std::string& name);
  bool UnregisterPlayer(int player_id);

  bool Start();

  bool ProcessHandComplete(int table_id, const std::vector<std::pair<int, double>>& results);

  void AdvanceBlindLevel();

  void RebalanceTables();
  void BalanceTables();

  void EliminatePlayer(int player_id);
  void AwardBounty(int eliminator_id, int eliminated_id);

  bool ProcessRebuy(int player_id);
  bool ProcessAddon(int player_id);

  void ComputePayouts();

  TournamentStatus Status() const { return status_; }
  int CurrentBlindLevel() const { return current_blind_level_; }
  BlindLevel CurrentBlinds() const;
  double CurrentBigBlind() const;
  double CurrentSmallBlind() const;
  double CurrentAnte() const;

  int ActivePlayerCount() const;
  int RemainingPlayerCount() const;
  int TableCount() const;

  const TournamentConfig& Config() const { return config_; }
  const TournamentPlayer* GetPlayer(int player_id) const;
  const TournamentTable* GetTable(int table_id) const;

  double PrizePool() const;
  double GetPayout(int position) const;
  int GetPaidPositions() const;

  bool IsBubble() const;
  bool IsFinalTable() const;

  const std::vector<TournamentEvent>& EventLog() const { return events_; }
  void LogEvent(TournamentEvent::Type type, int player_id = 0, double amount = 0.0,
                const std::string& detail = "");

  struct TournamentResult {
    int total_hands = 0;
    int total_levels = 0;
    double prize_pool = 0.0;
    std::vector<std::pair<int, double>> payouts;
    std::vector<TournamentPlayer> final_standings;
    std::vector<TournamentEvent> events;
    std::string ToString() const;
  };

  TournamentResult GetResult() const;

  void SetHandCallback(std::function<void(const TournamentEvent&)> cb) {
    hand_callback_ = std::move(cb);
  }
  void SetEliminationCallback(std::function<void(int, int)> cb) {
    elimination_callback_ = std::move(cb);
  }

 private:
  void AssignPlayersToTables();
  void RemovePlayerFromTable(int player_id, int table_id);
  int FindShortestTable() const;
  void MergeTable(int from_table_id);

  TournamentConfig config_;
  TournamentStatus status_ = TournamentStatus::Registration;

  std::unordered_map<int, TournamentPlayer> players_;
  std::unordered_map<int, TournamentTable> tables_;

  int current_blind_level_ = 0;
  int hands_played_ = 0;
  int next_table_id_ = 0;

  std::vector<TournamentEvent> events_;
  std::vector<std::pair<int, double>> payouts_;

  std::function<void(const TournamentEvent&)> hand_callback_;
  std::function<void(int, int)> elimination_callback_;

  std::mt19937 rng_{42};
  mutable std::mutex mutex_;
};

class TournamentBuilder {
 public:
  TournamentBuilder& WithName(const std::string& name);
  TournamentBuilder& WithType(TournamentType type);
  TournamentBuilder& WithBuyIn(double buy_in, double fee = 0.0);
  TournamentBuilder& WithStartingStack(int stack);
  TournamentBuilder& WithMaxPlayers(int max);
  TournamentBuilder& WithPlayersPerTable(int count);
  TournamentBuilder& WithGuarantee(double amount);

  TournamentBuilder& WithRebuys(int max_rebuys, double cost, double stack, int through_level = 6);
  TournamentBuilder& WithAddon(double cost, double stack);
  TournamentBuilder& WithBounty(double amount);

  TournamentBuilder& WithLateRegistration(int minutes);

  TournamentBuilder& WithTurboBlinds();
  TournamentBuilder& WithDeepStackBlinds();
  TournamentBuilder& WithCustomBlinds(const std::vector<BlindLevel>& schedule);

  TournamentBuilder& WithPayouts(const std::vector<double>& percentages);

  TournamentConfig Build() const;

 private:
  TournamentConfig config_;
};

}  // namespace poker_engine::tournament
