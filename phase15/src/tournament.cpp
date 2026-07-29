#include "poker_engine/tournament/tournament.h"

#include <fmt/format.h>

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <random>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::tournament {

// ==================== BlindLevel ====================

std::string BlindLevel::ToString() const {
  std::ostringstream oss;
  oss << "L" << level << ": " << small_blind << "/" << big_blind;
  if (ante > 0) oss << " ante=" << ante;
  oss << " (" << duration_minutes << "min)";
  return oss.str();
}

// ==================== TournamentConfig ====================

std::vector<BlindLevel> TournamentConfig::GenerateTurboBlinds(double starting_sb, int num_levels) {
  std::vector<BlindLevel> schedule;
  double sb = starting_sb;
  for (int i = 0; i < num_levels; ++i) {
    BlindLevel level;
    level.level = i + 1;
    level.small_blind = sb;
    level.big_blind = sb * 2;
    level.ante = (i >= 3) ? sb : 0.0;
    level.duration_minutes = 5;
    schedule.push_back(level);
    sb *= (i % 3 == 2) ? 2.0 : 1.5;
  }
  return schedule;
}

std::vector<BlindLevel> TournamentConfig::GenerateDeepStackBlinds(double starting_sb,
                                                                  int num_levels) {
  std::vector<BlindLevel> schedule;
  double sb = starting_sb;
  for (int i = 0; i < num_levels; ++i) {
    BlindLevel level;
    level.level = i + 1;
    level.small_blind = sb;
    level.big_blind = sb * 2;
    level.ante = (i >= 5) ? sb * 0.25 : 0.0;
    level.duration_minutes = 30;
    schedule.push_back(level);
    sb *= (i % 4 == 3) ? 2.0 : 1.5;
  }
  return schedule;
}

std::string TournamentConfig::ToString() const {
  std::ostringstream oss;
  oss << "Tournament: " << name << "\n";
  oss << "  Type: " << TournamentTypeName(type) << "\n";
  oss << "  Buy-in: " << buy_in << "+" << entry_fee << "\n";
  oss << "  Starting stack: " << starting_stack << "\n";
  oss << "  Max players: " << max_players << " (" << players_per_table << "/table)\n";
  if (guaranteed_prize_pool > 0) oss << "  Guarantee: " << guaranteed_prize_pool << "\n";
  if (has_rebuys)
    oss << "  Rebuys: " << max_rebuys << " @ " << rebuy_cost << " (through L" << rebuy_through_level
        << ")\n";
  if (has_bounty) oss << "  Bounty: " << bounty_amount << "\n";
  oss << "  Blind levels: " << blind_schedule.size() << "\n";
  return oss.str();
}

// ==================== TournamentPlayer ====================

std::string TournamentPlayer::ToString() const {
  std::ostringstream oss;
  oss << name << " (id=" << id << ", chips=" << chips << ")";
  if (eliminated) oss << " [ELIMINATED]";
  return oss.str();
}

// ==================== TournamentTable ====================

int TournamentTable::ActivePlayers() const { return static_cast<int>(player_ids.size()); }

std::string TournamentTable::ToString() const {
  std::ostringstream oss;
  oss << "Table " << id << ": " << player_ids.size() << "/" << max_seats;
  return oss.str();
}

// ==================== TournamentEvent ====================

std::string TournamentEvent::ToString() const {
  std::ostringstream oss;
  oss << TournamentEventTypeName(type);
  if (player_id > 0) oss << " player=" << player_id;
  if (amount > 0) oss << " amount=" << amount;
  if (!detail.empty()) oss << " " << detail;
  return oss.str();
}

// ==================== TournamentManager ====================

TournamentManager::TournamentManager(const TournamentConfig& config) : config_(config) {
  if (config_.blind_schedule.empty()) {
    config_.blind_schedule = TournamentConfig::GenerateTurboBlinds();
  }
}

bool TournamentManager::RegisterPlayer(int player_id, const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (status_ != TournamentStatus::Registration && status_ != TournamentStatus::LateRegistration) {
    PE_LOG_WARN("Cannot register: tournament status={}", TournamentStatusName(status_));
    return false;
  }

  if (static_cast<int>(players_.size()) >= config_.max_players) {
    PE_LOG_WARN("Cannot register: tournament full");
    return false;
  }

  if (players_.count(player_id) > 0) {
    PE_LOG_WARN("Player {} already registered", player_id);
    return false;
  }

  TournamentPlayer player;
  player.id = player_id;
  player.name = name;
  player.chips = config_.starting_stack;
  player.starting_stack = config_.starting_stack;
  player.total_invested = config_.buy_in + config_.entry_fee;

  if (config_.has_bounty) {
    player.bounty_on_head = config_.bounty_amount;
  }

  players_[player_id] = player;

  LogEvent(TournamentEvent::Type::PlayerRegistered, player_id, config_.buy_in, name);

  return true;
}

bool TournamentManager::UnregisterPlayer(int player_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (status_ != TournamentStatus::Registration) return false;

  auto it = players_.find(player_id);
  if (it == players_.end()) return false;

  players_.erase(it);
  LogEvent(TournamentEvent::Type::PlayerRegistered, player_id, 0, "unregister");
  return true;
}

bool TournamentManager::Start() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (status_ != TournamentStatus::Registration) {
    PE_LOG_WARN("Cannot start: status={}", TournamentStatusName(status_));
    return false;
  }

  int active = 0;
  for (auto& [id, p] : players_) {
    if (p.active) active++;
  }

  if (active < 2) {
    PE_LOG_WARN("Cannot start: need at least 2 players, have {}", active);
    return false;
  }

  status_ = TournamentStatus::Running;
  AssignPlayersToTables();

  LogEvent(TournamentEvent::Type::TournamentStart, 0, PrizePool());
  PE_LOG_INFO("Tournament started: {} players, {} tables", active, tables_.size());

  return true;
}

void TournamentManager::AssignPlayersToTables() {
  std::vector<int> player_ids;
  for (auto& [id, p] : players_) {
    if (p.active) player_ids.push_back(id);
  }

  std::shuffle(player_ids.begin(), player_ids.end(), rng_);

  tables_.clear();
  next_table_id_ = 0;

  int table_count = std::max(1, static_cast<int>(player_ids.size()) / config_.players_per_table);
  if (static_cast<int>(player_ids.size()) % config_.players_per_table > 0) table_count++;

  for (int t = 0; t < table_count; ++t) {
    TournamentTable table;
    table.id = next_table_id_++;
    table.max_seats = config_.players_per_table;
    tables_[table.id] = table;
  }

  for (size_t i = 0; i < player_ids.size(); ++i) {
    int table_idx = i % table_count;
    int table_id = 0;
    int idx = 0;
    for (auto& [tid, tbl] : tables_) {
      if (idx == table_idx) {
        table_id = tid;
        break;
      }
      idx++;
    }

    int seat = static_cast<int>(tables_[table_id].player_ids.size());
    tables_[table_id].player_ids.push_back(player_ids[i]);
    players_[player_ids[i]].table_id = table_id;
    players_[player_ids[i]].seat = seat;
  }
}

bool TournamentManager::ProcessHandComplete(int table_id,
                                            const std::vector<std::pair<int, double>>& results) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (status_ != TournamentStatus::Running && status_ != TournamentStatus::FinalTable) return false;

  hands_played_++;

  auto it = tables_.find(table_id);
  if (it == tables_.end()) return false;

  it->second.hand_count++;
  double pot = 0;
  for (auto& [pid, amount] : results) {
    pot += std::abs(amount);
    auto pit = players_.find(pid);
    if (pit != players_.end()) {
      pit->second.chips += amount;
      pit->second.hands_played++;
      if (amount > 0) pit->second.total_won += amount;
    }
  }
  it->second.total_pot += pot;

  std::vector<int> to_eliminate;
  for (auto& [pid, p] : players_) {
    if (p.active && !p.eliminated && p.chips <= 0 && p.table_id == table_id) {
      to_eliminate.push_back(pid);
    }
  }

  for (int pid : to_eliminate) {
    EliminatePlayer(pid);
  }

  LogEvent(TournamentEvent::Type::HandCompleted, 0, pot, "table=" + std::to_string(table_id));

  if (ActivePlayerCount() <= 1) {
    ComputePayouts();
    status_ = TournamentStatus::Completed;
    LogEvent(TournamentEvent::Type::TournamentEnd, 0, PrizePool());
  }

  return true;
}

void TournamentManager::EliminatePlayer(int player_id) {
  auto it = players_.find(player_id);
  if (it == players_.end()) return;

  TournamentPlayer& player = it->second;
  player.active = false;
  player.eliminated = true;
  player.finish_position = RemainingPlayerCount();

  int table_id = player.table_id;
  RemovePlayerFromTable(player_id, table_id);

  LogEvent(TournamentEvent::Type::PlayerEliminated, player_id, 0,
           "pos=" + std::to_string(player.finish_position));

  if (elimination_callback_) {
    elimination_callback_(player_id, 0);
  }

  if (IsFinalTable() && status_ == TournamentStatus::Running) {
    status_ = TournamentStatus::FinalTable;
    LogEvent(TournamentEvent::Type::HandCompleted, 0, 0, "final_table_reached");
  }
}

void TournamentManager::AwardBounty(int eliminator_id, int eliminated_id) {
  if (!config_.has_bounty) return;

  auto elim_it = players_.find(eliminator_id);
  auto dead_it = players_.find(eliminated_id);

  if (elim_it != players_.end() && dead_it != players_.end()) {
    double bounty = dead_it->second.bounty_on_head;
    elim_it->second.bounties_won += bounty;
    elim_it->second.chips += bounty;
    dead_it->second.bounty_on_head = 0;
  }
}

void TournamentManager::AdvanceBlindLevel() {
  std::lock_guard<std::mutex> lock(mutex_);

  current_blind_level_++;
  if (current_blind_level_ >= static_cast<int>(config_.blind_schedule.size())) {
    current_blind_level_ = static_cast<int>(config_.blind_schedule.size()) - 1;
  }

  auto& level = config_.blind_schedule[current_blind_level_];
  LogEvent(TournamentEvent::Type::BlindLevelUp, 0, level.big_blind,
           "L" + std::to_string(level.level) + ": " + std::to_string(level.small_blind) + "/" +
               std::to_string(level.big_blind));
}

void TournamentManager::RebalanceTables() {
  std::lock_guard<std::mutex> lock(mutex_);
  BalanceTables();
}

void TournamentManager::BalanceTables() {
  if (tables_.size() <= 1) return;

  std::vector<int> table_ids;
  for (auto& [id, tbl] : tables_) {
    if (tbl.ActivePlayers() > 0) table_ids.push_back(id);
  }

  if (table_ids.size() <= 1) return;

  int min_players = std::numeric_limits<int>::max();
  int max_players = 0;
  for (auto tid : table_ids) {
    int count = tables_[tid].ActivePlayers();
    min_players = std::min(min_players, count);
    max_players = std::max(max_players, count);
  }

  if (max_players - min_players <= 1) return;

  for (auto tid : table_ids) {
    if (tables_[tid].ActivePlayers() == 0) {
      MergeTable(tid);
    }
  }
}

void TournamentManager::RemovePlayerFromTable(int player_id, int table_id) {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return;

  auto& player_list = it->second.player_ids;
  player_list.erase(std::remove(player_list.begin(), player_list.end(), player_id),
                    player_list.end());
}

int TournamentManager::FindShortestTable() const {
  int min_count = std::numeric_limits<int>::max();
  int min_table = -1;
  for (auto& [id, tbl] : tables_) {
    int count = tbl.ActivePlayers();
    if (count > 0 && count < min_count) {
      min_count = count;
      min_table = id;
    }
  }
  return min_table;
}

void TournamentManager::MergeTable(int from_table_id) {
  auto from_it = tables_.find(from_table_id);
  if (from_it == tables_.end()) return;

  int target_id = FindShortestTable();
  if (target_id < 0 || target_id == from_table_id) return;

  auto& from_players = from_it->second.player_ids;
  auto& target = tables_[target_id];

  for (int pid : from_players) {
    target.player_ids.push_back(pid);
    players_[pid].table_id = target_id;
    players_[pid].seat = static_cast<int>(target.player_ids.size()) - 1;
  }

  from_players.clear();
  LogEvent(TournamentEvent::Type::TableRebalance, 0, 0,
           "merge table " + std::to_string(from_table_id) + " -> " + std::to_string(target_id));
}

bool TournamentManager::ProcessRebuy(int player_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!config_.has_rebuys) return false;
  if (current_blind_level_ >= config_.rebuy_through_level) return false;

  auto it = players_.find(player_id);
  if (it == players_.end()) return false;

  TournamentPlayer& player = it->second;
  if (player.rebuys_used >= config_.max_rebuys) return false;
  if (!player.eliminated && player.chips > 0) return false;

  player.chips = config_.rebuy_stack;
  player.rebuys_used++;
  player.active = true;
  player.eliminated = false;
  player.total_invested += config_.rebuy_cost;

  LogEvent(TournamentEvent::Type::PlayerRebuy, player_id, config_.rebuy_cost,
           "rebuy #" + std::to_string(player.rebuys_used));

  return true;
}

bool TournamentManager::ProcessAddon(int player_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!config_.has_addon) return false;

  auto it = players_.find(player_id);
  if (it == players_.end()) return false;

  TournamentPlayer& player = it->second;
  if (player.addon_used) return false;

  player.chips += config_.addon_stack;
  player.addon_used = true;
  player.total_invested += config_.addon_cost;

  LogEvent(TournamentEvent::Type::PlayerAddon, player_id, config_.addon_cost,
           "addon +" + std::to_string(config_.addon_stack));

  return true;
}

BlindLevel TournamentManager::CurrentBlinds() const {
  if (current_blind_level_ < static_cast<int>(config_.blind_schedule.size())) {
    return config_.blind_schedule[current_blind_level_];
  }
  BlindLevel fallback;
  fallback.level = current_blind_level_ + 1;
  fallback.small_blind = 1000;
  fallback.big_blind = 2000;
  return fallback;
}

double TournamentManager::CurrentBigBlind() const { return CurrentBlinds().big_blind; }

double TournamentManager::CurrentSmallBlind() const { return CurrentBlinds().small_blind; }

double TournamentManager::CurrentAnte() const { return CurrentBlinds().ante; }

int TournamentManager::ActivePlayerCount() const {
  int count = 0;
  for (auto& [id, p] : players_) {
    if (p.active && !p.eliminated) count++;
  }
  return count;
}

int TournamentManager::RemainingPlayerCount() const {
  int count = 0;
  for (auto& [id, p] : players_) {
    if (!p.eliminated) count++;
  }
  return count;
}

int TournamentManager::TableCount() const {
  int count = 0;
  for (auto& [id, tbl] : tables_) {
    if (tbl.ActivePlayers() > 0) count++;
  }
  return count;
}

const TournamentPlayer* TournamentManager::GetPlayer(int player_id) const {
  auto it = players_.find(player_id);
  return it != players_.end() ? &it->second : nullptr;
}

const TournamentTable* TournamentManager::GetTable(int table_id) const {
  auto it = tables_.find(table_id);
  return it != tables_.end() ? &it->second : nullptr;
}

double TournamentManager::PrizePool() const {
  double pool = 0;
  for (auto& [id, p] : players_) {
    pool += config_.buy_in;
    if (config_.has_rebuys) pool += p.rebuys_used * config_.rebuy_cost;
    if (config_.has_addon && p.addon_used) pool += config_.addon_cost;
  }
  return std::max(pool, config_.guaranteed_prize_pool);
}

int TournamentManager::GetPaidPositions() const {
  return static_cast<int>(config_.payout_percentages.size());
}

double TournamentManager::GetPayout(int position) const {
  if (position < 0 || position >= static_cast<int>(config_.payout_percentages.size())) {
    return 0.0;
  }
  return PrizePool() * config_.payout_percentages[position] / 100.0;
}

bool TournamentManager::IsBubble() const {
  int remaining = RemainingPlayerCount();
  int paid = GetPaidPositions();
  return remaining > 0 && remaining <= paid + 1 && remaining > paid;
}

bool TournamentManager::IsFinalTable() const {
  return ActivePlayerCount() <= config_.players_per_table && ActivePlayerCount() > 1;
}

void TournamentManager::LogEvent(TournamentEvent::Type type, int player_id, double amount,
                                 const std::string& detail) {
  TournamentEvent event;
  event.type = type;
  event.player_id = player_id;
  event.hand_id = hands_played_;
  event.level = current_blind_level_;
  event.amount = amount;
  event.detail = detail;
  events_.push_back(event);

  if (hand_callback_) hand_callback_(event);
}

void TournamentManager::ComputePayouts() {
  payouts_.clear();

  std::vector<std::pair<int, double>> standings;
  for (auto& [id, p] : players_) {
    standings.push_back({id, p.chips});
  }

  std::sort(standings.begin(), standings.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  double pool = PrizePool();
  for (int i = 0; i < static_cast<int>(config_.payout_percentages.size()) &&
                  i < static_cast<int>(standings.size());
       ++i) {
    double payout = pool * config_.payout_percentages[i] / 100.0;
    payouts_.push_back({standings[i].first, payout});
    players_[standings[i].first].total_won += payout;
  }
}

TournamentManager::TournamentResult TournamentManager::GetResult() const {
  TournamentResult result;
  result.total_hands = hands_played_;
  result.total_levels = current_blind_level_ + 1;
  result.prize_pool = PrizePool();
  result.payouts = payouts_;
  result.events = events_;

  for (auto& [id, p] : players_) {
    result.final_standings.push_back(p);
  }

  std::sort(result.final_standings.begin(), result.final_standings.end(),
            [](const TournamentPlayer& a, const TournamentPlayer& b) {
              if (a.eliminated != b.eliminated) return !a.eliminated;
              if (!a.eliminated && !b.eliminated) return a.chips > b.chips;
              return a.finish_position < b.finish_position;
            });

  return result;
}

// ==================== TournamentBuilder ====================

TournamentBuilder& TournamentBuilder::WithName(const std::string& name) {
  config_.name = name;
  return *this;
}

TournamentBuilder& TournamentBuilder::WithType(TournamentType type) {
  config_.type = type;
  return *this;
}

TournamentBuilder& TournamentBuilder::WithBuyIn(double buy_in, double fee) {
  config_.buy_in = buy_in;
  config_.entry_fee = fee;
  return *this;
}

TournamentBuilder& TournamentBuilder::WithStartingStack(int stack) {
  config_.starting_stack = stack;
  config_.rebuy_stack = stack;
  return *this;
}

TournamentBuilder& TournamentBuilder::WithMaxPlayers(int max) {
  config_.max_players = max;
  return *this;
}

TournamentBuilder& TournamentBuilder::WithPlayersPerTable(int count) {
  config_.players_per_table = count;
  return *this;
}

TournamentBuilder& TournamentBuilder::WithGuarantee(double amount) {
  config_.guaranteed_prize_pool = amount;
  return *this;
}

TournamentBuilder& TournamentBuilder::WithRebuys(int max_rebuys, double cost, double stack,
                                                 int through_level) {
  config_.has_rebuys = true;
  config_.max_rebuys = max_rebuys;
  config_.rebuy_cost = cost;
  config_.rebuy_stack = stack;
  config_.rebuy_through_level = through_level;
  return *this;
}

TournamentBuilder& TournamentBuilder::WithAddon(double cost, double stack) {
  config_.has_addon = true;
  config_.addon_cost = cost;
  config_.addon_stack = stack;
  return *this;
}

TournamentBuilder& TournamentBuilder::WithBounty(double amount) {
  config_.has_bounty = true;
  config_.bounty_amount = amount;
  return *this;
}

TournamentBuilder& TournamentBuilder::WithLateRegistration(int minutes) {
  config_.late_registration_minutes = minutes;
  return *this;
}

TournamentBuilder& TournamentBuilder::WithTurboBlinds() {
  config_.blind_schedule = TournamentConfig::GenerateTurboBlinds();
  return *this;
}

TournamentBuilder& TournamentBuilder::WithDeepStackBlinds() {
  config_.blind_schedule = TournamentConfig::GenerateDeepStackBlinds();
  return *this;
}

TournamentBuilder& TournamentBuilder::WithCustomBlinds(const std::vector<BlindLevel>& schedule) {
  config_.blind_schedule = schedule;
  return *this;
}

TournamentBuilder& TournamentBuilder::WithPayouts(const std::vector<double>& percentages) {
  config_.payout_percentages = percentages;
  return *this;
}

TournamentConfig TournamentBuilder::Build() const { return config_; }

}  // namespace poker_engine::tournament
