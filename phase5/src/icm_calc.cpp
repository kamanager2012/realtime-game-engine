#include "poker_engine/phase5/icm_calc.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace poker_engine {
namespace phase5 {

std::string ICMResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "\n=== ICM Analysis ===\n\n";
  oss << "Player  Equity($)  BubbleF  M-zone  RiskPrem\n";
  oss << std::string(50, '-') << "\n";
  for (int i = 0; i < 10; i++) {
    if (equity[i] == 0 && i > 0) continue;
    oss << std::setw(4) << i + 1 << "    " << std::setw(10) << equity[i] << std::setw(9)
        << bubble_factor[i] << std::setw(8) << m_zone[i] << std::setw(9) << risk_premium[i] << "\n";
  }
  return oss.str();
}

void ICMCalculator::ICMRecurse(double equity[], const double remaining[], int n,
                               const double payouts[], int payout_count, int place, double prob) {
  if (place >= payout_count) return;

  double total = 0;
  for (int i = 0; i < n; i++) total += remaining[i];
  if (total <= 0) return;

  for (int i = 0; i < n; i++) {
    if (remaining[i] <= 0) continue;
    double p_first = remaining[i] / total;
    equity[i] += prob * p_first * payouts[place];

    double saved = remaining[i];
    double rem_copy[10];
    for (int j = 0; j < n; j++) rem_copy[j] = remaining[j];
    rem_copy[i] = 0;

    ICMRecurse(equity, rem_copy, n, payouts, payout_count, place + 1, prob * p_first);
  }
}

ICMResult ICMCalculator::Calculate(const double payouts[], int payout_count, const double chips[],
                                   int player_count, double big_blind) {
  ICMResult result;
  std::memset(result.equity, 0, sizeof(result.equity));
  std::memset(result.bubble_factor, 0, sizeof(result.bubble_factor));
  std::memset(result.m_zone, 0, sizeof(result.m_zone));
  std::memset(result.risk_premium, 0, sizeof(result.risk_premium));

  int n = std::min(player_count, 10);

  double remaining[10];
  for (int i = 0; i < n; i++) remaining[i] = chips[i];
  for (int i = n; i < 10; i++) remaining[i] = 0;

  ICMRecurse(result.equity, remaining, n, payouts, payout_count, 0, 1.0);

  // M-zone
  double total_blinds_per_round = big_blind * 2;  // SB + BB (ante omitted)
  for (int i = 0; i < n; i++) {
    result.m_zone[i] = chips[i] / total_blinds_per_round;
  }

  // Bubble factor: ratio of equity lost when doubling up vs equity gained
  for (int i = 0; i < n; i++) {
    double current_equity = result.equity[i];

    // Simulate: player i wins a hand against player j (j = next smallest stack)
    int j = (i + 1) % n;
    double doubled_chips[10];
    for (int k = 0; k < n; k++) doubled_chips[k] = chips[k];
    doubled_chips[i] = chips[i] + chips[j];
    doubled_chips[j] = 0;

    double doubled_equity[10] = {0};
    double rem[10];
    for (int k = 0; k < n; k++) rem[k] = doubled_chips[k];
    ICMRecurse(doubled_equity, rem, n, payouts, payout_count, 0, 1.0);

    // Bubble factor = (equity_gained / chips_gained) / (equity_lost_by_victim / chips_lost)
    double equity_gained = doubled_equity[i] - current_equity;
    double equity_lost = current_equity - (chips[j] > 0 ? result.equity[j] : 0);
    result.bubble_factor[i] =
        (equity_gained > 0 && equity_lost > 0) ? equity_lost / equity_gained : 1.0;
  }

  // Risk premium
  double total_payout = 0;
  for (int i = 0; i < payout_count; i++) total_payout += payouts[i];

  for (int i = 0; i < n; i++) {
    double avg_equity = total_payout / n;
    result.risk_premium[i] =
        result.equity[i] > 0 ? (avg_equity - result.equity[i]) / result.equity[i] : 0;
  }

  return result;
}

double ICMCalculator::BubbleFactor(const double payouts[], int payout_count, const double chips[],
                                   int player_count, int player_idx) {
  auto result = Calculate(payouts, payout_count, chips, player_count);
  return result.bubble_factor[player_idx];
}

double ICMCalculator::MValue(double stack, double big_blind, double ante, int players) {
  double cost_per_round = big_blind + big_blind / 2 + ante * players;
  return stack / cost_per_round;
}

double ICMCalculator::RiskPremium(const double payouts[], int payout_count, const double chips[],
                                  int player_count, int player_idx) {
  auto result = Calculate(payouts, payout_count, chips, player_count);
  return result.risk_premium[player_idx];
}

std::vector<PushFoldEntry> ICMCalculator::PushFoldTable(double hero_stack_bb, double effective_bb) {
  std::vector<PushFoldEntry> table;

  // Simplified: compute equity needed for +EV push
  double dead_money = 1.5;  // SB + BB
  double equity_needed = hero_stack_bb / (hero_stack_bb + effective_bb + dead_money);

  // 169 abstract hand types
  const char* ranks = "23456789TJQKA";
  const char* types[] = {"s", "o"};  // suited, offsuit (pairs are separate)

  int hand_idx = 0;

  // Pocket pairs: 22-AA
  for (int i = 0; i < 13; i++) {
    PushFoldEntry entry;
    entry.hand_rank = hand_idx++;
    char name[4];
    name[0] = ranks[i];
    name[1] = ranks[i];
    name[2] = '\0';
    entry.hand_name = name;

    // Approximate pair equity vs random range
    double pair_equity = 0.5 + (i / 12.0) * 0.35;  // 22~50%, AA~85%

    entry.equity_needed = equity_needed;
    entry.ev_push = (pair_equity * (hero_stack_bb + effective_bb + dead_money)) -
                    ((1.0 - pair_equity) * hero_stack_bb);
    entry.ev_fold = 0;
    entry.push = entry.ev_push > entry.ev_fold;

    table.push_back(entry);
  }

  // Suited hands: AKs, AQs, ..., 32s
  for (int hi = 12; hi >= 0; hi--) {
    for (int lo = hi - 1; lo >= 0; lo--) {
      PushFoldEntry entry;
      entry.hand_rank = hand_idx++;
      char name[5];
      name[0] = ranks[hi];
      name[1] = ranks[lo];
      name[2] = 's';
      name[3] = '\0';
      entry.hand_name = name;

      double suited_equity = 0.4 + (hi / 12.0) * 0.2 + (hi - lo <= 2 ? 0.05 : 0);
      entry.equity_needed = equity_needed;
      entry.ev_push = (suited_equity * (hero_stack_bb + effective_bb + dead_money)) -
                      ((1.0 - suited_equity) * hero_stack_bb);
      entry.ev_fold = 0;
      entry.push = entry.ev_push > entry.ev_fold;

      table.push_back(entry);
    }
  }

  // Offsuit hands: AKo, AQo, ..., 32o
  for (int hi = 12; hi >= 0; hi--) {
    for (int lo = hi - 1; lo >= 0; lo--) {
      PushFoldEntry entry;
      entry.hand_rank = hand_idx++;
      char name[5];
      name[0] = ranks[hi];
      name[1] = ranks[lo];
      name[2] = 'o';
      name[3] = '\0';
      entry.hand_name = name;

      double offsuit_equity = 0.38 + (hi / 12.0) * 0.18 + (hi - lo <= 2 ? 0.03 : 0);
      entry.equity_needed = equity_needed;
      entry.ev_push = (offsuit_equity * (hero_stack_bb + effective_bb + dead_money)) -
                      ((1.0 - offsuit_equity) * hero_stack_bb);
      entry.ev_fold = 0;
      entry.push = entry.ev_push > entry.ev_fold;

      table.push_back(entry);
    }
  }

  return table;
}

double ICMCalculator::PressureIndex(const double payouts[], int payout_count, const double chips[],
                                    int player_count, int player_idx) {
  auto result = Calculate(payouts, payout_count, chips, player_count);
  double max_m = 0;
  for (int i = 0; i < player_count; i++) max_m = std::max(max_m, result.m_zone[i]);
  if (max_m <= 0) return 0;
  return 1.0 - result.m_zone[player_idx] / max_m;
}

}  // namespace phase5
}  // namespace poker_engine
