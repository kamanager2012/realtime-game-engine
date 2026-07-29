#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase7 {

struct BlindLevel {
  int level = 0;
  double small_blind = 0;
  double big_blind = 0;
  double ante = 0;
  int duration_minutes = 10;
};

struct MTTConfig {
  std::string name = "MTT Simulation";
  int starting_stack = 1500;
  int max_players_per_table = 9;
  int target_players = 54;
  int num_tables = 6;
  double prize_pool_percentage = 1.0;
  bool has_rebuys = false;
  int max_rebuys = 1;
  double rebuy_cost = 0;
  double rebuy_stack = 1500;
  bool has_addons = false;
  double addon_cost = 0;
  double addon_stack = 3000;
  std::vector<double> payout_percentages = {50, 30, 20};
  std::vector<BlindLevel> blind_schedule;
  bool verbose = false;
};

struct MTTPlayer {
  int id = 0;
  std::string name;
  double chips = 0;
  int table = -1;
  int seat = -1;
  int starting_stack = 0;
  bool active = true;
  bool sitting_out = false;
  int rebuys_left = 0;
  bool busted_rebuy = false;
  int hands_played = 0;
  double total_won = 0;
  double total_invested = 0;
  int vpip_count = 0;
  int pfr_count = 0;
  bool IsInGame() const { return active && !sitting_out && chips > 0; }
  double StackInBB(double big_blind) const { return big_blind > 0 ? chips / big_blind : 0; }
};

struct MTTAction {
  enum Type { FOLD, CHECK, CALL, BET, RAISE, ALL_IN, POST_SB, POST_BB, ANTE };
  int player_id = 0;
  Type type = FOLD;
  double amount = 0;
  int street = 0;
  std::string ToString() const;
};

struct MTTHandResult {
  int hand_id = 0;
  std::vector<int> winners;
  std::vector<double> winnings;
  double pot_size = 0;
  int table_num = 0;
  std::string ToString() const;
};

struct MTTActionLog {
  int hand_id;
  std::string player;
  std::string action;
  double amount;
  double pot_after;
};

struct MTTResult {
  int total_hands = 0;
  int total_levels = 0;
  double total_prize_pool = 0;
  std::vector<std::pair<int, double>> payouts;
  std::vector<MTTPlayer> final_standings;
  std::vector<MTTHandResult> hand_results;
  std::vector<MTTActionLog> action_log;
  double first_in_avg_bb = 0;
  double avg_players_per_level = 0;
  std::string ToString() const;
};

class MTTSimulator {
 public:
  explicit MTTSimulator(const MTTConfig& config);
  void SetPlayerName(int id, const std::string& name);
  void SetStrategy(int id, const std::string& range_str);
  void SetAllRandomStrategies();
  MTTResult Run();
  void SetCallback(std::function<void(const MTTPlayer&, const MTTActionLog&)> cb);
  void SetHandPlayedCallback(std::function<void(const MTTHandResult&)> cb);

 private:
  MTTConfig config_;
  std::vector<MTTPlayer> players_;
  std::map<int, poker_engine::range::Range> strategies_;
  std::map<int, std::string> player_names_;
  std::mt19937 rng_{42};
  int hand_counter_ = 0;
  int current_blind_level_ = 0;
  std::function<void(const MTTPlayer&, const MTTActionLog&)> callback_;
  std::function<void(const MTTHandResult&)> hand_callback_;
  void SetupTables();
  void AssignPlayersToTables();
  void RebalanceTables();
  std::vector<MTTPlayer*> GetActivePlayers(int table);
  std::vector<MTTPlayer*> GetActivePlayersAllTables();
  void PlayHand(int table);
  MTTHandResult PlaySingleHand(std::vector<MTTPlayer*>& table_players, int table_num);
  bool IsBubble(int total_alive);
  bool CanCash(int total_alive);
  double GetPayout(int rank, double prize_pool, int total_paid);
  int GetNumPaid();
  double GetCurrentBB();
  double GetCurrentSB();
  double GetCurrentAnte();
  void AdvanceBlindLevel();
  MTTAction DecideAction(MTTPlayer& player, double pot, double to_call, int street,
                         std::mt19937& rng);
};

}  // namespace phase7
}  // namespace poker_engine
