#include "poker_engine/phase8/preflop_solver.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/range/hand_id.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase8 {
using namespace poker_engine::range;
using namespace poker_engine::equity;
using namespace poker_engine::evaluator;

std::string PreflopHandInfo::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << hand_name;
  if (suited)
    oss << "s";
  else if (!hand_name.empty() && hand_name.size() == 2 && hand_name[0] != hand_name[1])
    oss << "o";
  oss << "\n  vs 1b stacks: ";
  for (int p = 0; p < NUM_POSITIONS; p++)
    if (equity_vs_1bbs[p] > 0)
      oss << PositionName[p] << ":" << int(equity_vs_1bbs[p] * 100) << "% ";
  oss << "\n  vs 3b pots: ";
  for (int p = 0; p < NUM_POSITIONS; p++)
    if (equity_vs_3bbs[p] > 0)
      oss << PositionName[p] << ":" << int(equity_vs_3bbs[p] * 100) << "% ";
  return oss.str();
}

std::string PreflopAdvice::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << std::setw(6) << PositionName[position] << " | " << std::setw(6) << hand_name << " | "
      << std::setw(16) << std::left << recommended_action << std::right << " | R:" << std::setw(3)
      << int(raise_freq * 100) << "%"
      << " C:" << std::setw(3) << int(call_freq * 100) << "%"
      << " F:" << std::setw(3) << int(fold_freq * 100) << "%";
  return oss.str();
}

void PreflopMatrix169::Init() {
  int idx = 0;
  const char* ranks = "AKQJT98765432";
  for (int r = 0; r < 13; r++) labels[idx++] = std::string(1, ranks[r]) + std::string(1, ranks[r]);
  for (int r1 = 0; r1 < 13; r1++)
    for (int r2 = r1 + 1; r2 < 13; r2++)
      labels[idx++] = std::string(1, ranks[r1]) + std::string(1, ranks[r2]) + "s";
  for (int r1 = 0; r1 < 13; r1++)
    for (int r2 = r1 + 1; r2 < 13; r2++)
      labels[idx++] = std::string(1, ranks[r1]) + std::string(1, ranks[r2]) + "o";
  for (; idx < NUM_TYPES; idx++) labels[idx] = "XX";
}

double PreflopMatrix169::GetEquity(int hero_idx, int villain_idx, int) const {
  if (hero_idx < 0 || hero_idx >= NUM_TYPES || villain_idx < 0 || villain_idx >= NUM_TYPES)
    return 0.5;
  return equity_matrix[hero_idx][villain_idx];
}

PreflopSolver::PreflopSolver(const PreflopConfig& config) { Init(config); }

void PreflopSolver::Init(const PreflopConfig& cfg) {
  config_ = cfg;
  ComputeAllEquities();
}

void PreflopSolver::ComputeAllEquities() {
  equity_matrix_.Init();
  for (int h = 0; h < PreflopMatrix169::NUM_TYPES; h++) {
    const std::string& hh = equity_matrix_.labels[h];
    if (hh.empty() || hh == "XX") continue;
    Range hero = Range::FromString(hh);
    for (int v = 0; v <= h; v++) {
      const std::string& vv = equity_matrix_.labels[v];
      if (vv.empty() || vv == "XX") continue;
      Range villain = Range::FromString(vv);
      double eq = ComputeEquityVsRange(hero, villain, config_.mc_samples / 10);
      equity_matrix_.equity_matrix[h][v] = eq;
      equity_matrix_.equity_matrix[v][h] = 1.0 - eq;
    }
  }
}

double PreflopSolver::ComputeEquityVsRange(const Range& hero, const Range& villain, int n_samples) {
  uint8_t board5[5] = {0};
  std::mt19937 rng(rng_());
  auto res = poker_engine::equity::EquityCalculator::CalculateMonteCarlo(hero, villain, board5, 0,
                                                                         n_samples, rng);
  return res.equity[0];
}

int PreflopSolver::HandToIndex(uint8_t c1, uint8_t c2) {
  int r1 = c1 / 4, r2 = c2 / 4;
  bool suited = (c1 % 4) == (c2 % 4);
  int lo = std::min(r1, r2), hi = std::max(r1, r2);
  int idx = 0;
  if (lo == hi) return 12 - hi;
  idx = 13;
  for (int rr = 12; rr > hi; rr--) idx += rr;
  idx += (hi - lo - 1);
  if (!suited) idx += 78;
  return std::min(idx, 168);
}

std::vector<PreflopAdvice> PreflopSolver::SolvePosition(PreflopPosition pos) {
  std::vector<PreflopAdvice> advice_list;

  bool early = (pos == UTG || pos == UTG1 || pos == UTG2);
  bool middle = (pos == MP || pos == MP1);
  bool late = (pos == HJ || pos == CO || pos == BTN);
  bool blind = (pos == SB || pos == BB);

  double raise_threshold = early ? 0.10 : (late ? 0.05 : 0.07);
  double call_threshold = early ? 0.30 : (late ? 0.45 : 0.35);
  double raise3_threshold = early ? 0.03 : (late ? 0.06 : 0.04);

  // Build hand list with equities
  struct HandInfo {
    std::string name;
    double equity;
    double eq_vs_3bet;
  };
  std::vector<HandInfo> all_hands;

  std::string opp_range_str;
  double vpip = 10 + ((NUM_POSITIONS - 1 - pos) * 5.0);
  if (vpip < 15)
    opp_range_str = "TT+,AKs+,AKo";
  else if (vpip < 25)
    opp_range_str = "77+,A9s+,K9s+,Q9s+,J9s+,T9s+,98s+";
  else
    opp_range_str = "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,A2o+,K2o+";

  Range opponent = Range::FromString(opp_range_str);
  Range tight_3bet = Range::FromString("TT+,AKs+,AKo");

  for (int i = 0; i < PreflopMatrix169::NUM_TYPES; i++) {
    const std::string& label = equity_matrix_.labels[i];
    if (label == "XX") continue;
    Range hero = Range::FromString(label);
    double eq = ComputeEquityVsRange(hero, opponent, 2000);
    double eq_3b = ComputeEquityVsRange(hero, tight_3bet, 2000);
    all_hands.push_back({label, eq, eq_3b});
  }

  std::sort(all_hands.begin(), all_hands.end(),
            [](auto& a, auto& b) { return a.equity > b.equity; });

  for (size_t i = 0; i < all_hands.size(); i++) {
    double rank_pct = (double)i / all_hands.size();
    PreflopAdvice advice;
    advice.position = pos;
    advice.hand_name = all_hands[i].name;
    advice.ev_vs_1bb = all_hands[i].equity * 2 - 1;
    advice.ev_vs_3bb = all_hands[i].eq_vs_3bet * 2 - 1;

    if (rank_pct < raise_threshold) {
      if (rank_pct < raise3_threshold) {
        advice.recommended_action = "3Bet";
        advice.raise_freq = 0.7;
        advice.call_freq = 0.3;
        advice.fold_freq = 0.0;
        advice.min_3bet_pct = 0.8;
        advice.min_raise_pct = 0.5;
      } else {
        advice.recommended_action = "Raise";
        advice.raise_freq = 0.85;
        advice.call_freq = 0.15;
        advice.fold_freq = 0.0;
        advice.min_raise_pct = 0.7;
        advice.min_3bet_pct = 0.3;
      }
    } else if (rank_pct < call_threshold) {
      advice.recommended_action = "Call";
      advice.raise_freq = 0.0;
      advice.call_freq = 0.8;
      advice.fold_freq = 0.2;
    } else if (rank_pct < 0.7) {
      if (late && rank_pct < 0.55) {
        advice.recommended_action = "Mix (Call/Fold)";
        advice.raise_freq = 0.0;
        advice.call_freq = 0.4;
        advice.fold_freq = 0.6;
      } else {
        advice.recommended_action = "Fold";
        advice.raise_freq = 0.0;
        advice.call_freq = 0.05;
        advice.fold_freq = 0.95;
      }
    } else {
      advice.recommended_action = "Always Fold";
      advice.raise_freq = 0.0;
      advice.call_freq = 0.0;
      advice.fold_freq = 1.0;
    }

    advice_list.push_back(advice);
  }
  return advice_list;
}

std::map<PreflopPosition, std::vector<PreflopAdvice>> PreflopSolver::SolveAll() {
  std::map<PreflopPosition, std::vector<PreflopAdvice>> all;
  if (config_.positions.empty()) config_.positions = {BTN, SB, BB, UTG, HJ, CO};
  for (auto pos : config_.positions) {
    all[pos] = SolvePosition(pos);
    if (config_.verbose) std::cout << "\rSolved " << PositionName[pos] << std::flush;
  }
  if (config_.verbose) std::cout << "\n";
  return all;
}

PreflopHandInfo PreflopSolver::AnalyzeHand(const std::string& hand_str, PreflopPosition) {
  PreflopHandInfo info;
  info.hand_name = hand_str;
  info.suited = (hand_str.size() >= 3 && hand_str.back() == 's');

  Range hand_range = Range::FromString(hand_str);
  if (hand_range.NonZeroCount() != 1) {
    info.card1 = info.card2 = 0xFF;
    return info;
  }

  for (int i = 0; i < 1326; i++) {
    if (hand_range.Get(i) > 0) {
      auto [c1, c2] = HandId::Decode(static_cast<uint16_t>(i));
      info.card1 = c1;
      info.card2 = c2;
      break;
    }
  }

  std::vector<std::string> ranges_by_pos(NUM_POSITIONS);
  for (int p = 0; p < NUM_POSITIONS; p++) {
    double vpip = 10 + ((NUM_POSITIONS - 1 - p) * 5.0);
    if (vpip < 15)
      ranges_by_pos[p] = "TT+,AKs+,AKo";
    else if (vpip < 25)
      ranges_by_pos[p] = "77+,A9s+,K9s+";
    else
      ranges_by_pos[p] = "22+,A2s+,K2s+";
  }

  for (int p = 0; p < NUM_POSITIONS; p++) {
    Range opp = Range::FromString(ranges_by_pos[p]);
    uint8_t board5[5] = {0};
    std::mt19937 rng(rng_());
    auto res = poker_engine::equity::EquityCalculator::CalculateMonteCarlo(hand_range, opp, board5,
                                                                           0, 2000, rng);
    info.equity_vs_1bbs[p] = res.equity[0];
    info.equity_vs_3bbs[p] = res.equity[0];
  }
  return info;
}

PreflopMatrix169 PreflopSolver::GetEquityMatrix() {
  if (equity_matrix_.labels[0].empty()) ComputeAllEquities();
  return equity_matrix_;
}

std::vector<PreflopAdvice> PreflopSolver::QuickSolve(PreflopPosition pos, int n_samples) {
  PreflopConfig config;
  config.iterations = 200;
  config.mc_samples = n_samples;
  config.positions = {pos};
  config.verbose = false;
  PreflopSolver solver(config);
  return solver.SolveAll()[pos];
}

}  // namespace phase8
}  // namespace poker_engine
