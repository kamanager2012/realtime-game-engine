#include "poker_engine/anticheat/anticheat.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <numeric>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::anticheat {

namespace {
double ComputeMean(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
}
double ComputeStdDev(const std::vector<double>& v, double mean) {
  if (v.size() < 2) return 0.0;
  double variance = 0.0;
  for (auto x : v) variance += (x - mean) * (x - mean);
  return std::sqrt(variance / (v.size() - 1));
}
double ComputeZScore(double value, double mean, double stddev) {
  if (stddev < 1e-10) return 0.0;
  return (value - mean) / stddev;
}
std::string SuspicionLevelToString(SuspicionLevel level) {
  switch (level) {
    case SuspicionLevel::Clean:
      return "CLEAN";
    case SuspicionLevel::Low:
      return "LOW";
    case SuspicionLevel::Medium:
      return "MEDIUM";
    case SuspicionLevel::High:
      return "HIGH";
    case SuspicionLevel::Confirmed:
      return "CONFIRMED";
  }
  return "UNKNOWN";
}
}  // namespace

std::string CheatAlert::ToString() const {
  std::ostringstream oss;
  oss << "Player " << player_id;
  if (!player_name.empty()) oss << " (" << player_name << ")";
  oss << " - [" << SuspicionLevelToString(level) << "] " << reason << " (score: " << std::fixed
      << std::setprecision(1) << score << ")";
  return oss.str();
}

void PlayerStatistics::ComputeRatios() {
  vpip_pct = hands_played > 0 ? (100.0 * vpip_hands / hands_played) : 0.0;
  pfr_pct = hands_played > 0 ? (100.0 * pfr_hands / hands_played) : 0.0;
  agg_factor = calls > 0 ? (1.0 * bets_raises / calls) : 0.0;
  three_bet_pct = pfr_hands > 0 ? (100.0 * three_bet_hands / pfr_hands) : 0.0;
  estimated_range_pct = vpip_pct / 100.0;
}
double PlayerStatistics::ZScore(double value, double mean, double stddev) {
  return ComputeZScore(value, mean, stddev);
}
double PlayerStatistics::Mean(const std::vector<double>& values) { return ComputeMean(values); }
double PlayerStatistics::StdDev(const std::vector<double>& values, double mean) {
  return ComputeStdDev(values, mean);
}

// CollusionDetector
CollusionDetector::CollusionDetector(const CollusionConfig& config) : config_(config) {}
CollusionDetector::CollusionDetector() : CollusionDetector(CollusionConfig()) {}
void CollusionDetector::AddPlayerStats(const PlayerStatistics& stats) { players_.push_back(stats); }

double CollusionDetector::ComputeSameTableRate(const PlayerStatistics& a,
                                               const PlayerStatistics& b) const {
  auto it = a.same_table_counts.find(b.player_id);
  if (it == a.same_table_counts.end()) return 0.0;
  int min_hands = std::min(a.hands_played, b.hands_played);
  return min_hands > 0 ? static_cast<double>(it->second) / min_hands : 0.0;
}
double CollusionDetector::ComputeAdjacentSeatRate(const PlayerStatistics& a,
                                                  const PlayerStatistics& b) const {
  auto it = a.adjacent_seat_counts.find(b.player_id);
  if (it == a.adjacent_seat_counts.end()) return 0.0;
  int min_hands = std::min(a.hands_played, b.hands_played);
  return min_hands > 0 ? static_cast<double>(it->second) / min_hands : 0.0;
}
double CollusionDetector::ComputeSuspiciousActions(const PlayerStatistics& a,
                                                   const PlayerStatistics& b) const {
  double avg_a = a.bet_sizing_history.empty() ? 0.5 : ComputeMean(a.bet_sizing_history);
  double avg_b = b.bet_sizing_history.empty() ? 0.5 : ComputeMean(b.bet_sizing_history);
  double avg_both = (avg_a + avg_b) / 2.0;
  return (avg_both < config_.soft_play_threshold) ? 1.0 : 0.0;
}
double CollusionDetector::ComputeMutualFoldRate(const PlayerStatistics& a,
                                                const PlayerStatistics& b) const {
  double a_ep_vpip = (a.early.hands > 0) ? (100.0 * a.early.vpip / a.early.hands) : 0;
  double b_ep_vpip = (b.early.hands > 0) ? (100.0 * b.early.vpip / b.early.hands) : 0;
  double low_vpip_score = 0;
  if (a_ep_vpip < 15 && b_ep_vpip < 15) low_vpip_score = 0.5;
  if (a_ep_vpip < 10 && b_ep_vpip < 10) low_vpip_score = 0.8;
  return low_vpip_score;
}

CollusionDetector::PlayerPairAnalysis CollusionDetector::AnalyzePair(
    const PlayerStatistics& a, const PlayerStatistics& b) const {
  PlayerPairAnalysis analysis{};
  analysis.player_a = a.player_id;
  analysis.player_b = b.player_id;
  if (a.hands_played < config_.min_hands_for_analysis ||
      b.hands_played < config_.min_hands_for_analysis) {
    analysis.overall_score = 0.0;
    analysis.evidence = "{\"reason\":\"insufficient_data\"}";
    return analysis;
  }
  analysis.same_table_rate = ComputeSameTableRate(a, b);
  analysis.adjacent_seat_rate = ComputeAdjacentSeatRate(a, b);
  analysis.suspicious_bet_timing = ComputeSuspiciousActions(a, b);
  analysis.mutual_fold_rate = ComputeMutualFoldRate(a, b);
  double score = analysis.same_table_rate * 25.0 + analysis.adjacent_seat_rate * 20.0 +
                 analysis.suspicious_bet_timing * 25.0 + analysis.mutual_fold_rate * 30.0;
  analysis.overall_score = std::min(100.0, score);
  nlohmann::json evidence;
  evidence["same_table_rate"] = analysis.same_table_rate;
  evidence["adjacent_seat_rate"] = analysis.adjacent_seat_rate;
  evidence["suspicious_bet_timing"] = analysis.suspicious_bet_timing;
  evidence["mutual_fold_rate"] = analysis.mutual_fold_rate;
  analysis.evidence = evidence.dump();
  return analysis;
}

std::vector<CollusionDetector::PlayerPairAnalysis> CollusionDetector::AnalyzeAllPairs() {
  std::vector<PlayerPairAnalysis> results;
  for (size_t i = 0; i < players_.size(); ++i) {
    for (size_t j = i + 1; j < players_.size(); ++j) {
      auto analysis = AnalyzePair(players_[i], players_[j]);
      if (analysis.overall_score > 10.0) results.push_back(analysis);
    }
  }
  std::sort(results.begin(), results.end(),
            [](const auto& a, const auto& b) { return a.overall_score > b.overall_score; });
  return results;
}

SuspicionLevel CollusionDetector::PlayerPairAnalysis::GetLevel() const {
  if (overall_score >= 60) return SuspicionLevel::Confirmed;
  if (overall_score >= 40) return SuspicionLevel::High;
  if (overall_score >= 20) return SuspicionLevel::Medium;
  if (overall_score >= 10) return SuspicionLevel::Low;
  return SuspicionLevel::Clean;
}

// BotDetector
BotDetector::BotDetector(const BotConfig& config) : config_(config) {}
BotDetector::BotDetector() : BotDetector(BotConfig()) {}
void BotDetector::AddPlayerStats(const PlayerStatistics& stats) { players_.push_back(stats); }

double BotDetector::ComputeResponseConsistency(const PlayerStatistics& stats) const {
  if (stats.response_times_ms.size() < 10) return 0.5;
  std::vector<double> rt(stats.response_times_ms.begin(), stats.response_times_ms.end());
  double mean = ComputeMean(rt);
  double stddev = ComputeStdDev(rt, mean);
  if (mean < 1e-10) return 1.0;
  double cv = stddev / mean;
  if (cv < config_.response_time_consistency_threshold) return 1.0;
  if (cv > config_.reaction_time_fluctuation_max) return 0.0;
  return 1.0 -
         (cv - config_.response_time_consistency_threshold) /
             (config_.reaction_time_fluctuation_max - config_.response_time_consistency_threshold);
}
double BotDetector::ComputeOptimalPlayRatio(const PlayerStatistics& stats) const {
  if (stats.vpip_pct > 0 && stats.pfr_pct > 0) {
    double pfr_vpip_ratio = stats.pfr_pct / stats.vpip_pct;
    double deviation = std::abs(pfr_vpip_ratio - 0.7);
    return std::max(0.0, 1.0 - deviation / 0.5);
  }
  return 0.0;
}
double BotDetector::ComputeBetSizingPrecision(const PlayerStatistics& stats) const {
  if (stats.bet_sizing_history.size() < 20) return 0.5;
  std::vector<double> sizes = stats.bet_sizing_history;
  std::sort(sizes.begin(), sizes.end());
  auto last = std::unique(sizes.begin(), sizes.end(),
                          [](double a, double b) { return std::abs(a - b) < 0.01; });
  int unique_count = std::distance(sizes.begin(), last);
  double ratio = static_cast<double>(unique_count) / stats.bet_sizing_history.size();
  return std::min(1.0, ratio * 5.0);
}
double BotDetector::ComputePositionAwareness(const PlayerStatistics& stats) const {
  auto compute_pos_diff = [](const PlayerStatistics::PositionalStats& a,
                             const PlayerStatistics::PositionalStats& b) {
    if (a.hands == 0 || b.hands == 0) return 0.0;
    return std::abs(100.0 * a.vpip / a.hands - 100.0 * b.vpip / b.hands);
  };
  double max_diff = 0.0;
  const PlayerStatistics::PositionalStats* positions[] = {&stats.early, &stats.middle, &stats.late,
                                                          &stats.blind};
  for (int i = 0; i < 4; ++i)
    for (int j = i + 1; j < 4; ++j)
      max_diff = std::max(max_diff, compute_pos_diff(*positions[i], *positions[j]));
  if (max_diff < config_.position_awareness_threshold)
    return 1.0;
  else if (max_diff < 10.0)
    return 0.5;
  return 0.0;
}

BotDetector::BotAnalysis BotDetector::AnalyzePlayer(const PlayerStatistics& stats) const {
  BotAnalysis analysis{};
  analysis.player_id = stats.player_id;
  analysis.response_consistency_score = ComputeResponseConsistency(stats);
  analysis.optimal_play_score = ComputeOptimalPlayRatio(stats);
  analysis.bet_sizing_score = ComputeBetSizingPrecision(stats);
  analysis.position_awareness = ComputePositionAwareness(stats);
  analysis.overall_bot_probability =
      analysis.response_consistency_score * 0.30 + analysis.optimal_play_score * 0.25 +
      analysis.bet_sizing_score * 0.20 + analysis.position_awareness * 0.25;
  if (analysis.response_consistency_score > 0.8)
    analysis.flags.push_back("CONSISTENT_RESPONSE_TIME");
  if (analysis.optimal_play_score > 0.8) analysis.flags.push_back("EXCESSIVELY_OPTIMAL_PLAY");
  if (analysis.bet_sizing_score > 0.8) analysis.flags.push_back("PRECISION_BET_SIZING");
  if (analysis.position_awareness < 0.3) analysis.flags.push_back("NO_POSITIONAL_AWARENESS");
  nlohmann::json evidence;
  evidence["response_consistency"] = analysis.response_consistency_score;
  evidence["optimal_play"] = analysis.optimal_play_score;
  evidence["bet_sizing"] = analysis.bet_sizing_score;
  evidence["position_awareness"] = analysis.position_awareness;
  evidence["vpip_pct"] = stats.vpip_pct;
  evidence["pfr_pct"] = stats.pfr_pct;
  evidence["agg_factor"] = stats.agg_factor;
  evidence["hands_played"] = stats.hands_played;
  analysis.evidence = evidence.dump();
  return analysis;
}
std::vector<BotDetector::BotAnalysis> BotDetector::AnalyzeAll() {
  std::vector<BotAnalysis> results;
  for (auto& stats : players_) results.push_back(AnalyzePlayer(stats));
  return results;
}
SuspicionLevel BotDetector::BotAnalysis::GetLevel() const {
  if (overall_bot_probability >= 0.75) return SuspicionLevel::Confirmed;
  if (overall_bot_probability >= 0.55) return SuspicionLevel::High;
  if (overall_bot_probability >= 0.35) return SuspicionLevel::Medium;
  if (overall_bot_probability >= 0.15) return SuspicionLevel::Low;
  return SuspicionLevel::Clean;
}

// AntiCheatManager
AntiCheatManager::AntiCheatManager()
    : collusion_detector_(CollusionDetector::CollusionConfig()),
      bot_detector_(BotDetector::BotConfig()) {}
void AntiCheatManager::SubmitHandData(const replay::HandSnapshot& hand) { UpdatePlayerStats(hand); }
void AntiCheatManager::SubmitHandBatch(const std::vector<replay::HandSnapshot>& hands) {
  for (auto& h : hands) UpdatePlayerStats(h);
}

void AntiCheatManager::UpdatePlayerStats(const replay::HandSnapshot& hand) {
  for (auto& player : hand.players) {
    auto& stats = player_stats_[player.player_id];
    stats.player_id = player.player_id;
    stats.player_name = player.display_name;
    stats.hands_played++;
    int64_t profit = player.chips_at_end - player.chips_at_start;
    stats.total_profit += profit;
    if (profit > 0) stats.hands_won++;
    for (auto& action_str : player.actions) {
      if (action_str.find("call") == 0) {
        stats.vpip_hands++;
        stats.calls++;
      } else if (action_str.find("raise") == 0 || action_str.find("bet") == 0) {
        stats.vpip_hands++;
        stats.pfr_hands++;
        stats.bets_raises++;
        stats.three_bet_hands++;
        try {
          size_t pos = action_str.find_last_of(' ');
          if (pos != std::string::npos) {
            double amount = std::stod(action_str.substr(pos + 1));
            if (hand.total_pot > 0) stats.bet_sizing_history.push_back(amount / hand.total_pot);
          }
        } catch (...) {
        }
      }
    }
  }
  if (hand.players.size() >= 2) {
    for (size_t i = 0; i < hand.players.size(); ++i) {
      for (size_t j = i + 1; j < hand.players.size(); ++j) {
        int64_t id_a = hand.players[i].player_id, id_b = hand.players[j].player_id;
        player_stats_[id_a].same_table_counts[id_b]++;
        player_stats_[id_b].same_table_counts[id_a]++;
        int seat_diff = std::abs(hand.players[i].seat_index - hand.players[j].seat_index);
        if (seat_diff == 1 || seat_diff == static_cast<int>(hand.players.size()) - 1) {
          player_stats_[id_a].adjacent_seat_counts[id_b]++;
          player_stats_[id_b].adjacent_seat_counts[id_a]++;
        }
      }
    }
  }
}

void AntiCheatManager::RunAnalysis() {
  PE_LOG_INFO("AntiCheat: Running analysis on {} players", player_stats_.size());
  alerts_.clear();
  for (auto& [pid, stats] : player_stats_) stats.ComputeRatios();
  for (auto& [pid, stats] : player_stats_) collusion_detector_.AddPlayerStats(stats);
  auto collusion_results = collusion_detector_.AnalyzeAllPairs();
  for (auto& pair : collusion_results) {
    SuspicionLevel level = pair.GetLevel();
    if (level > SuspicionLevel::Low) {
      GenerateAlert(pair.player_a, level, "Collusion suspected", pair.evidence);
      GenerateAlert(pair.player_b, level, "Collusion suspected", pair.evidence);
    }
  }
  for (auto& [pid, stats] : player_stats_) bot_detector_.AddPlayerStats(stats);
  auto bot_results = bot_detector_.AnalyzeAll();
  for (auto& analysis : bot_results) {
    SuspicionLevel level = analysis.GetLevel();
    if (level > SuspicionLevel::Low)
      GenerateAlert(analysis.player_id, level, "Bot behavior suspected", analysis.evidence);
  }
  PE_LOG_INFO("AntiCheat: Generated {} alerts", alerts_.size());
  for (auto& alert : alerts_)
    if (alert_callback_) alert_callback_(alert);
}

double AntiCheatManager::GetPlayerSuspicionScore(int64_t player_id) const {
  double max_score = 0.0;
  for (auto& alert : alerts_)
    if (alert.player_id == player_id) max_score = std::max(max_score, alert.score);
  return max_score;
}
const PlayerStatistics* AntiCheatManager::GetPlayerStats(int64_t player_id) const {
  auto it = player_stats_.find(player_id);
  return it != player_stats_.end() ? &it->second : nullptr;
}
void AntiCheatManager::Reset() {
  player_stats_.clear();
  alerts_.clear();
  collusion_detector_ = CollusionDetector();
  bot_detector_ = BotDetector();
}

void AntiCheatManager::GenerateAlert(int64_t player_id, SuspicionLevel level,
                                     const std::string& reason, const std::string& evidence) {
  CheatAlert alert;
  alert.player_id = player_id;
  alert.level = level;
  alert.reason = reason;
  alert.evidence = evidence;
  alert.score = static_cast<double>(static_cast<uint8_t>(level)) /
                static_cast<double>(static_cast<uint8_t>(SuspicionLevel::Confirmed)) * 100.0;
  alert.timestamp = 0;
  auto it = player_stats_.find(player_id);
  if (it != player_stats_.end()) alert.player_name = it->second.player_name;
  alerts_.push_back(alert);
}

}  // namespace poker_engine::anticheat
