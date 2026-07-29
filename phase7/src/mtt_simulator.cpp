#include "poker_engine/phase7/mtt_simulator.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

#include "poker_engine/evaluator/card.h"

namespace poker_engine {
namespace phase7 {
using namespace poker_engine::range;

std::string MTTAction::ToString() const {
  static const char* names[] = {"FOLD",   "CHECK",   "CALL",    "BET", "RAISE",
                                "ALL_IN", "POST_SB", "POST_BB", "ANTE"};
  return names[static_cast<int>(type)] + (amount > 0 ? " $" + std::to_string((int)amount) : "");
}

std::string MTTHandResult::ToString() const {
  std::ostringstream oss;
  oss << "Hand #" << hand_id << " | Table " << table_num << " | Pot: $" << int(pot_size)
      << " | Winners: ";
  for (size_t i = 0; i < winners.size(); i++) {
    oss << "P" << winners[i];
    if (i < winnings.size()) oss << " $" << int(winnings[i]);
    if (i + 1 < winners.size()) oss << ", ";
  }
  return oss.str();
}

std::string MTTResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "=== MTT Simulation Result ===\nLevels: " << total_levels << " | Hands: " << total_hands
      << " | Prize Pool: $" << int(total_prize_pool) << "\n\n";
  oss << std::setw(8) << "Rank" << std::setw(12) << "Player" << std::setw(12) << "Stack"
      << std::setw(12) << "Prize" << "\n";
  oss << std::string(46, '-') << "\n";
  for (const auto& p : final_standings) {
    double prize = 0;
    for (const auto& [pid, amt] : payouts)
      if (pid == p.id) prize = amt;
    oss << std::setw(8) << p.id << std::setw(12) << p.name << std::setw(12) << int(p.chips)
        << std::setw(12) << (prize > 0 ? "$" + std::to_string((int)prize) : "-") << "\n";
  }
  return oss.str();
}

MTTSimulator::MTTSimulator(const MTTConfig& config) : config_(config) { rng_.seed(42); }
void MTTSimulator::SetPlayerName(int id, const std::string& name) { player_names_[id] = name; }
void MTTSimulator::SetStrategy(int id, const std::string& range_str) {
  strategies_[id] = Range::FromString(range_str);
}
void MTTSimulator::SetAllRandomStrategies() {
  for (auto& p : players_) strategies_[p.id] = Range::FullCombinatorial();
}
void MTTSimulator::SetCallback(std::function<void(const MTTPlayer&, const MTTActionLog&)> cb) {
  callback_ = cb;
}
void MTTSimulator::SetHandPlayedCallback(std::function<void(const MTTHandResult&)> cb) {
  hand_callback_ = cb;
}

void MTTSimulator::SetupTables() {
  players_.clear();
  for (int i = 0; i < config_.target_players; i++) {
    MTTPlayer p;
    p.id = i + 1;
    p.name = player_names_.count(p.id) ? player_names_[p.id] : ("Player" + std::to_string(p.id));
    p.chips = config_.starting_stack;
    p.starting_stack = config_.starting_stack;
    p.rebuys_left = config_.max_rebuys;
    p.active = true;
    players_.push_back(p);
  }
  AssignPlayersToTables();
  current_blind_level_ = 0;
}

void MTTSimulator::AssignPlayersToTables() {
  std::vector<MTTPlayer*> active;
  for (auto& p : players_)
    if (p.active) active.push_back(&p);
  int table_num = 0, seat = 0;
  config_.num_tables = std::max(1, (int)active.size() / config_.max_players_per_table);
  for (auto* p : active) {
    p->table = table_num;
    p->seat = seat++;
    if (seat >= config_.max_players_per_table) {
      seat = 0;
      table_num++;
    }
  }
}

std::vector<MTTPlayer*> MTTSimulator::GetActivePlayers(int table) {
  std::vector<MTTPlayer*> result;
  for (auto& p : players_)
    if (p.table == table && p.IsInGame()) result.push_back(&p);
  std::sort(result.begin(), result.end(),
            [](MTTPlayer* a, MTTPlayer* b) { return a->seat < b->seat; });
  return result;
}

std::vector<MTTPlayer*> MTTSimulator::GetActivePlayersAllTables() {
  std::vector<MTTPlayer*> result;
  for (auto& p : players_)
    if (p.IsInGame()) result.push_back(&p);
  return result;
}

void MTTSimulator::RebalanceTables() {
  for (int t = 0; t < config_.num_tables; t++) {
    auto tp = GetActivePlayers(t);
    if (tp.size() == 1) {
      for (int t2 = 0; t2 < config_.num_tables; t2++) {
        if (t2 != t) {
          auto tp2 = GetActivePlayers(t2);
          if (tp2.size() < config_.max_players_per_table) {
            tp[0]->table = t2;
            tp[0]->seat = static_cast<int>(tp2.size());
            break;
          }
        }
      }
    }
  }
}

double MTTSimulator::GetCurrentBB() {
  if (current_blind_level_ < static_cast<int>(config_.blind_schedule.size()))
    return config_.blind_schedule[current_blind_level_].big_blind;
  return config_.blind_schedule.back().big_blind * 2;
}
double MTTSimulator::GetCurrentSB() {
  if (current_blind_level_ < static_cast<int>(config_.blind_schedule.size()))
    return config_.blind_schedule[current_blind_level_].small_blind;
  return config_.blind_schedule.back().small_blind * 2;
}
double MTTSimulator::GetCurrentAnte() {
  if (current_blind_level_ < static_cast<int>(config_.blind_schedule.size()))
    return config_.blind_schedule[current_blind_level_].ante;
  return 0;
}
int MTTSimulator::GetNumPaid() { return static_cast<int>(config_.payout_percentages.size()); }
bool MTTSimulator::IsBubble(int total_alive) { return total_alive == GetNumPaid() + 1; }
bool MTTSimulator::CanCash(int total_alive) { return total_alive <= GetNumPaid(); }
double MTTSimulator::GetPayout(int rank, double prize_pool, int total_paid) {
  if (rank - 1 < static_cast<int>(config_.payout_percentages.size()))
    return prize_pool * config_.payout_percentages[rank - 1] / 100.0;
  return 0;
}

MTTAction MTTSimulator::DecideAction(MTTPlayer& player, double pot, double to_call, int street,
                                     std::mt19937& rng) {
  MTTAction action;
  action.player_id = player.id;
  action.amount = 0;
  action.street = street;
  double m = player.StackInBB(GetCurrentBB());
  int roll = rng() % 100;

  if (to_call == 0) {
    if (m < 5) {
      if (roll < 30) {
        action.type = MTTAction::ALL_IN;
        action.amount = player.chips;
      } else if (roll < 60) {
        action.type = MTTAction::BET;
        action.amount = GetCurrentBB() * 2;
      } else {
        action.type = MTTAction::CHECK;
      }
    } else if (m < 15) {
      if (roll < 25) {
        action.type = MTTAction::BET;
        action.amount = GetCurrentBB() * 2.5;
      } else if (roll < 35) {
        action.type = MTTAction::ALL_IN;
        action.amount = player.chips;
      } else {
        action.type = MTTAction::CHECK;
      }
    } else {
      if (roll < 30) {
        action.type = MTTAction::CHECK;
      } else if (roll < 60) {
        action.type = MTTAction::BET;
        action.amount = GetCurrentBB() * (2 + (rng() % 3));
      } else if (roll < 80) {
        action.type = MTTAction::CALL;
        action.amount = to_call;
      } else {
        action.type = MTTAction::FOLD;
      }
    }
  } else {
    double pot_odds = to_call / std::max(1.0, pot + to_call);
    if (pot_odds > 0.5 || m < 3) {
      if (m < 5 || roll < 40) {
        action.type = MTTAction::ALL_IN;
        action.amount = player.chips;
      } else {
        action.type = MTTAction::FOLD;
      }
    } else if (pot_odds > 0.3) {
      if (roll < 50) {
        action.type = MTTAction::CALL;
        action.amount = to_call;
      } else {
        action.type = MTTAction::FOLD;
      }
    } else {
      if (roll < 70) {
        action.type = MTTAction::CALL;
        action.amount = to_call;
      } else if (roll < 85) {
        action.type = MTTAction::RAISE;
        action.amount = pot;
      } else {
        action.type = MTTAction::FOLD;
      }
    }
  }
  if (action.type == MTTAction::BET || action.type == MTTAction::RAISE ||
      action.type == MTTAction::ALL_IN)
    action.amount = std::min(action.amount, player.chips);
  if (action.type == MTTAction::CALL) action.amount = std::min(to_call, player.chips);
  return action;
}

MTTHandResult MTTSimulator::PlaySingleHand(std::vector<MTTPlayer*>& table_players, int table_num) {
  MTTHandResult result;
  result.hand_id = ++hand_counter_;
  result.table_num = table_num;
  if (table_players.size() < 2) return result;

  double sb = GetCurrentSB(), bb = GetCurrentBB(), pot = 0;
  int dealer_idx = rng_() % static_cast<int>(table_players.size());
  int sb_idx = (dealer_idx + 1) % table_players.size();
  int bb_idx = (dealer_idx + 2) % table_players.size();

  std::vector<double> bet_amounts(table_players.size(), 0);
  std::vector<bool> folded(table_players.size(), false);
  std::vector<bool> acted(table_players.size(), false);
  int active_count = static_cast<int>(table_players.size());

  // Post SB/BB
  double actual_sb = std::min(sb, table_players[sb_idx]->chips);
  table_players[sb_idx]->chips -= actual_sb;
  bet_amounts[sb_idx] = actual_sb;
  pot += actual_sb;
  double actual_bb = std::min(bb, table_players[bb_idx]->chips);
  table_players[bb_idx]->chips -= actual_bb;
  bet_amounts[bb_idx] = actual_bb;
  pot += actual_bb;

  // Generate pseudo-random hole cards
  for (size_t i = 0; i < table_players.size(); i++) {
    (void)i;  // cards generated for simulation only
  }

  // Betting round lambda
  auto play_betting_round = [&](int street_idx) -> double {
    std::fill(acted.begin(), acted.end(), false);
    int last_raiser = -1;
    int remaining = 0;
    for (int i = 0; i < static_cast<int>(table_players.size()); i++)
      if (!folded[i]) remaining++;
    if (remaining <= 1) return pot;

    for (int i = 0; i < 100; i++) {
      int ap = (bb_idx + 1 + i) % static_cast<int>(table_players.size());
      if (folded[ap]) continue;
      if (acted[ap] && last_raiser < 0) break;

      double max_bet = *std::max_element(bet_amounts.begin(), bet_amounts.end());
      double to_call = max_bet - bet_amounts[ap];

      MTTAction action = DecideAction(*table_players[ap], pot, to_call, street_idx, rng_);

      if (action.type == MTTAction::FOLD) {
        folded[ap] = true;
        active_count--;
      } else if (action.type == MTTAction::CHECK) {
        acted[ap] = true;
      } else if (action.type == MTTAction::CALL) {
        double call_amt = std::min(action.amount, std::min(to_call, table_players[ap]->chips));
        table_players[ap]->chips -= call_amt;
        bet_amounts[ap] += call_amt;
        pot += call_amt;
        acted[ap] = true;
        table_players[ap]->hands_played++;
        table_players[ap]->total_invested += call_amt;
      } else if (action.type == MTTAction::BET || action.type == MTTAction::RAISE) {
        double bet_amt = std::min(action.amount, table_players[ap]->chips);
        bet_amt = std::max(bet_amt, to_call);
        double added = bet_amt - bet_amounts[ap];
        table_players[ap]->chips -= added;
        pot += added;
        bet_amounts[ap] = bet_amt;
        last_raiser = ap;
        for (int j = 0; j < static_cast<int>(table_players.size()); j++)
          if (j != ap && !folded[j]) acted[j] = false;
        acted[ap] = true;
        table_players[ap]->hands_played++;
        table_players[ap]->pfr_count++;
        table_players[ap]->total_invested += added;
      } else if (action.type == MTTAction::ALL_IN) {
        double allin = table_players[ap]->chips;
        table_players[ap]->chips = 0;
        double added = allin - bet_amounts[ap];
        pot += added;
        bet_amounts[ap] += allin;
        last_raiser = ap;
        acted[ap] = true;
        table_players[ap]->hands_played++;
      }

      int rem = 0;
      for (int j = 0; j < static_cast<int>(table_players.size()); j++)
        if (!folded[j]) rem++;
      if (rem <= 1) break;
    }
    return pot;
  };

  // Run 4 streets
  pot = play_betting_round(0);                        // preflop
  if (active_count > 1) pot = play_betting_round(1);  // flop
  if (active_count > 1) pot = play_betting_round(2);  // turn
  if (active_count > 1) pot = play_betting_round(3);  // river

  // Showdown: random winner among non-folded
  std::vector<int> survivors;
  for (int i = 0; i < static_cast<int>(table_players.size()); i++)
    if (!folded[i]) survivors.push_back(i);

  if (survivors.size() == 1) {
    int wi = survivors[0];
    table_players[wi]->chips += pot;
    table_players[wi]->total_won += pot;
    result.winners = {table_players[wi]->id};
    result.winnings = {pot};
  } else if (!survivors.empty()) {
    int wi = survivors[rng_() % survivors.size()];
    table_players[wi]->chips += pot;
    table_players[wi]->total_won += pot;
    result.winners = {table_players[wi]->id};
    result.winnings = {pot};
  }

  result.pot_size = pot;
  if (hand_callback_) hand_callback_(result);
  return result;
}

void MTTSimulator::PlayHand(int table) {
  auto players = GetActivePlayers(table);
  if (players.size() < 2) return;
  PlaySingleHand(players, table);

  for (auto& p : players_) {
    if (p.table == table && p.chips <= 0 && p.IsInGame()) {
      if (p.rebuys_left > 0 && config_.has_rebuys) {
        p.chips = config_.rebuy_stack;
        p.rebuys_left--;
        p.busted_rebuy = true;
        p.total_invested += config_.rebuy_cost;
      } else {
        p.active = false;
        p.sitting_out = false;
      }
    }
  }
}

void MTTSimulator::AdvanceBlindLevel() {
  current_blind_level_++;
  if (config_.verbose && current_blind_level_ < static_cast<int>(config_.blind_schedule.size())) {
    auto& level = config_.blind_schedule[current_blind_level_];
    std::cout << "\n>>> Level " << level.level << " SB:$" << level.small_blind << " BB:$"
              << level.big_blind;
    if (level.ante > 0) std::cout << " Ante:$" << level.ante;
    std::cout << std::endl;
  }
}

MTTResult MTTSimulator::Run() {
  SetupTables();
  MTTResult result;
  int max_levels =
      config_.blind_schedule.empty() ? 50 : static_cast<int>(config_.blind_schedule.size());

  for (int level = 0; level < max_levels; level++) {
    if (level < static_cast<int>(config_.blind_schedule.size())) current_blind_level_ = level;
    int hands_per_level = std::max(5, 10 - (level / 3));

    for (int h = 0; h < hands_per_level; h++) {
      int alive_count = static_cast<int>(GetActivePlayersAllTables().size());
      if (alive_count <= 1) break;
      for (int t = 0; t < config_.num_tables; t++) {
        auto tp = GetActivePlayers(t);
        if (tp.size() >= 2) PlayHand(t);
      }
      result.total_hands++;
    }

    result.total_levels++;
    AdvanceBlindLevel();
    RebalanceTables();
    if (GetActivePlayersAllTables().size() <= 1) break;
  }

  // Final payouts
  std::vector<MTTPlayer*> final_active = GetActivePlayersAllTables();
  std::sort(final_active.begin(), final_active.end(),
            [](MTTPlayer* a, MTTPlayer* b) { return a->chips > b->chips; });

  double total_prize_pool = 0;
  for (auto& p : players_) total_prize_pool += (config_.starting_stack + p.total_invested);
  if (total_prize_pool < 0) total_prize_pool = 0;

  // Assign payouts to top finishers
  // First, rank all players by chips (active first, then busted)
  std::vector<MTTPlayer*> all_ranked;
  for (auto* p : final_active) all_ranked.push_back(p);
  // Add busted players
  std::vector<MTTPlayer*> busted;
  for (auto& p : players_)
    if (!p.IsInGame()) busted.push_back(&p);
  std::sort(busted.begin(), busted.end(),
            [](MTTPlayer* a, MTTPlayer* b) { return a->chips > b->chips; });
  for (auto* p : busted) all_ranked.push_back(p);

  double paid_so_far = 0;
  for (size_t rank = 0; rank < all_ranked.size() && rank < config_.payout_percentages.size();
       rank++) {
    double prize = total_prize_pool * config_.payout_percentages[rank] / 100.0;
    result.payouts.push_back({all_ranked[rank]->id, prize});
    all_ranked[rank]->chips = prize;
    paid_so_far += prize;
  }

  for (auto& p : players_) result.final_standings.push_back(p);
  result.total_prize_pool = total_prize_pool;
  return result;
}

}  // namespace phase7
}  // namespace poker_engine
