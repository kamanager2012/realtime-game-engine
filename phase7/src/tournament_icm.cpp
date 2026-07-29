#include "poker_engine/phase7/tournament_icm.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace poker_engine {
namespace phase7 {

std::string PushFoldAdvice::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << "Eff: " << eff_stack << " BB | PotOdds: " << pot_odds << " | Action: " << action
      << " | PushEq: " << push_min_equity * 100 << "% | ICM_Pressure: " << icm_pressure;
  return oss.str();
}

std::string TourneyICMResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "=== Tournament ICM Results ===\n\n";
  oss << std::setw(16) << std::left << "Player" << std::setw(12) << "Chips" << std::setw(8)
      << "Equity" << std::setw(10) << "$Equity" << std::setw(8) << "Bubble" << "\n";
  oss << std::string(58, '-') << "\n";
  for (const auto& p : players) {
    oss << std::setw(16) << std::left << p.name << std::setw(12) << (int)p.chips << std::setw(7)
        << (int)(p.equity * 100) << "%" << std::setw(10) << "$" << (int)p.icm_dollar << std::setw(8)
        << (int)(p.bubble_pressure * 100) << "%" << "\n";
  }
  oss << "\nBubble Factor Avg: " << bubble_factor_avg << "\n";
  oss << "On Bubble: " << (on_bubble ? "YES" : "NO") << "\n";
  if (!push_fold_table.empty()) {
    oss << "\n--- Push/Fold Table ---\n";
    for (const auto& advice : push_fold_table) oss << "  " << advice.ToString() << "\n";
  }
  return oss.str();
}

TournamentICM::TournamentICM(const TourneyConfig& cfg) : config_(cfg) {}
void TournamentICM::SetConfig(const TourneyConfig& cfg) { config_ = cfg; }
void TournamentICM::AddPlayer(const std::string& name, double chips) {
  players_.push_back({name, chips});
}
void TournamentICM::SetPayoutSchedule(const std::vector<double>& payouts) { payouts_ = payouts; }

void TournamentICM::ICMRecursive(std::vector<double>& equity, std::vector<double>& chips,
                                 double sum, int depth, double prob,
                                 const std::vector<double>& payouts) {
  int n = static_cast<int>(chips.size());
  if (depth >= static_cast<int>(payouts.size()) || depth >= n || sum <= 0 || depth > 20) return;

  for (int i = 0; i < n; i++) {
    if (chips[i] <= 0) continue;
    double prob_win = chips[i] / sum;
    double saved = chips[i];
    equity[i] += prob * prob_win * payouts[depth];
    chips[i] = 0;
    ICMRecursive(equity, chips, sum - saved, depth + 1, prob * prob_win, payouts);
    chips[i] = saved;
  }
}

TourneyICMResult TournamentICM::Calculate() { return Calculate(config_.total_prize_pool); }

TourneyICMResult TournamentICM::Calculate(double max_prize_pool) {
  TourneyICMResult result;
  int n = static_cast<int>(players_.size());
  if (n == 0) return result;

  std::vector<double> payouts(n, 0);
  if (!payouts_.empty()) {
    for (int i = 0; i < std::min(n, static_cast<int>(payouts_.size())); i++)
      payouts[i] = payouts_[i];
  } else {
    double remaining = max_prize_pool;
    for (int i = 0; i < n && remaining > 0; i++) {
      double share = 1.0 / (i + 1);
      payouts[i] = std::min(remaining * share, remaining);
      remaining -= payouts[i];
    }
  }

  std::vector<double> cash_equity(n, 0);
  std::vector<double> chips(n);
  for (int i = 0; i < n; i++) chips[i] = players_[i].second;
  double total_chips = std::accumulate(chips.begin(), chips.end(), 0.0);

  ICMRecursive(cash_equity, chips, total_chips, 0, 1.0, payouts);

  double total_equity = 0;
  for (int i = 0; i < n; i++) {
    TourneyPlayer player;
    player.name = players_[i].first;
    player.chips = players_[i].second;
    player.equity = (total_chips > 0 && max_prize_pool > 0) ? cash_equity[i] / max_prize_pool : 0;
    player.icm_dollar = cash_equity[i];
    total_equity += player.equity;
    double chips_pct = player.chips / total_chips;
    if (chips_pct > 0) player.bubble_pressure = std::max(0.0, 1.0 - (player.equity / chips_pct));
    result.players.push_back(player);
  }

  if (total_equity > 0)
    for (auto& p : result.players) p.equity /= total_equity;

  double bf_sum = 0;
  for (const auto& p : result.players) bf_sum += p.bubble_pressure;
  result.bubble_factor_avg = n > 0 ? bf_sum / n : 0;

  if (config_.players_remaining > 0 && config_.total_starting > 0) {
    double remaining_pct = (double)config_.players_remaining / config_.total_starting;
    result.on_bubble =
        remaining_pct < 0.15 && static_cast<int>(payouts_.size()) < config_.players_remaining;
  } else {
    result.on_bubble = result.bubble_factor_avg > 1.2;
  }

  result.push_fold_table =
      PushFoldTable(config_.starting_stack / config_.big_blind, config_.big_blind, false);
  result.total_equity = total_equity;
  return result;
}

std::vector<PushFoldAdvice> TournamentICM::PushFoldTable(double eff_stack, double pot,
                                                         bool is_heads_up) {
  std::vector<PushFoldAdvice> table;
  double stacks[] = {2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 25, 30, 50};
  for (double s : stacks) {
    PushFoldAdvice advice;
    advice.eff_stack = s;
    advice.pot_odds = pot / (s * config_.big_blind);
    if (s < 3) {
      advice.push_min_equity = 0.15;
      advice.call_min_equity = 0.10;
      advice.action = "PUSH (desperate)";
      advice.icm_pressure = 3.0;
    } else if (s < 6) {
      advice.push_min_equity = 0.25;
      advice.call_min_equity = 0.18;
      advice.action = "PUSH (short)";
      advice.icm_pressure = 2.0;
    } else if (s < 12) {
      advice.push_min_equity = 0.35;
      advice.call_min_equity = 0.22;
      advice.action = "STANDARD";
      advice.icm_pressure = 1.5;
    } else if (s < 25) {
      advice.push_min_equity = 0.45;
      advice.call_min_equity = 0.28;
      advice.action = "STANDARD";
      advice.icm_pressure = 1.0;
    } else {
      advice.push_min_equity = 0.55;
      advice.call_min_equity = 0.35;
      advice.action = "DEEP_STACK";
      advice.icm_pressure = 0.5;
    }
    advice.fold_equity = 0;
    table.push_back(advice);
  }
  return table;
}

double TournamentICM::PlayerEquity(const TourneyICMResult& result, const std::string& name) const {
  for (const auto& p : result.players)
    if (p.name == name) return p.equity;
  return 0;
}

double TournamentICM::NashPushRange(double m_value, double bb) {
  if (m_value < 1) return 0.95;
  if (m_value < 2) return 0.80;
  if (m_value < 3) return 0.60;
  if (m_value < 5) return 0.40;
  if (m_value < 8) return 0.25;
  if (m_value < 12) return 0.15;
  if (m_value < 20) return 0.10;
  return 0.05;
}

double TournamentICM::NashCallRange(double m_value, double bb, double pot_odds) {
  return NashPushRange(m_value, bb) * 0.6 * pot_odds;
}

TournamentICM::BubbleAnalysis TournamentICM::AnalyzeBubble(double stack, int position) {
  BubbleAnalysis ba;
  double m = stack / config_.big_blind;
  if (m < 1) {
    ba.elimination_probability = 0.9;
    ba.expected_doublings = 0.2;
    ba.suggested_action = "ALL-IN or fold immediately";
  } else if (m < 3) {
    ba.elimination_probability = 0.6;
    ba.expected_doublings = 0.5;
    ba.suggested_action = "Push aggressively, call wide";
  } else if (m < 6) {
    ba.elimination_probability = 0.35;
    ba.expected_doublings = 1.0;
    ba.suggested_action = "Push selectively, defend blinds";
  } else if (m < 12) {
    ba.elimination_probability = 0.15;
    ba.expected_doublings = 2.0;
    ba.suggested_action = "Standard push/fold, look for spots";
  } else {
    ba.elimination_probability = 0.05;
    ba.expected_doublings = 5.0;
    ba.suggested_action = "No immediate danger, play normal GTO";
  }
  if (position == 0)
    ba.suggested_action += " (UTG: tighter)";
  else if (position == 5)
    ba.suggested_action += " (BTN: wider)";
  return ba;
}

}  // namespace phase7
}  // namespace poker_engine
