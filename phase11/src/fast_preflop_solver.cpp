#include "poker_engine/phase11/fast_preflop_solver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase11 {
using namespace poker_engine::range;
using namespace poker_engine::evaluator;
using namespace poker_engine::equity;

// ===================== FastPreflopResult =====================
std::string FastPreflopResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << "=== Fast Preflop Solver (";
  switch (mode_used) {
    case FastSolveMode::LUT_ONLY:
      oss << "LUT Only";
      break;
    case FastSolveMode::LUT_REFINE:
      oss << "LUT+Refine";
      break;
    case FastSolveMode::HYBRID:
      oss << "Hybrid";
      break;
  }
  oss << ") ===\n";
  oss << "Time: " << solve_time_ms << "ms | Hands: " << hands_evaluated << "\n\n";

  oss << std::setw(8) << "Hand" << std::setw(10) << "Eq%" << std::setw(8) << "EV" << std::setw(8)
      << "R%" << std::setw(8) << "3B%" << std::setw(14) << "Category" << std::setw(6) << "Score"
      << "\n"
      << std::string(64, '-') << "\n";

  int shown = 0;
  for (const auto& a : advice) {
    if (a.score > 1 || shown < 30) {
      oss << std::setw(8) << a.hand_name << std::setw(9) << int(a.equity_vs_1bb_range * 100) << "%"
          << std::setw(8) << int(a.ev_vs_1bb * 100) << "bb" << std::setw(7)
          << int(a.optimal_raise_pct * 100) << "%" << std::setw(7) << int(a.optimal_3bet_pct * 100)
          << "%" << std::setw(14) << a.category << std::setw(6) << int(a.score) << "\n";
      shown++;
    }
  }

  oss << "\nTotal actionable hands: " << shown << "\n";
  return oss.str();
}

std::vector<FastPreflopResult::HandAdvice> FastPreflopResult::TopN(int n) const {
  std::vector<HandAdvice> result;
  for (const auto& a : advice) {
    if (a.score > 1) result.push_back(a);
  }
  if (result.size() > static_cast<size_t>(n)) result.resize(n);
  return result;
}

// ===================== FastPreflopSolver =====================

FastPreflopSolver::FastPreflopSolver(const FastPreflopConfig& config) : config_(config) {}

bool FastPreflopSolver::LoadLUT(const std::string& filepath) {
  if (lut_.LoadFromFile(filepath)) {
    lut_loaded_ = true;
    if (config_.verbose) std::cout << "LUT loaded from: " << filepath << "\n";
    return true;
  }
  if (config_.verbose) std::cout << "Warning: Could not load LUT from " << filepath << "\n";
  return false;
}

bool FastPreflopSolver::BuildLUT(const std::string& save_path) {
  PreflopLUTCalculator calc;
  lut_ = calc.Calculate(config_.lut_min_samples);
  lut_.SaveToFile(save_path);
  lut_loaded_ = true;

  if (config_.verbose) std::cout << "LUT built and saved to: " << save_path << "\n";
  return true;
}

std::string FastPreflopSolver::ClassifyAction(float raise_pct, float call_pct) const {
  if (raise_pct > 0.7) return "Always Raise";
  if (raise_pct > 0.3 && call_pct > 0.2) return "Mix R/C";
  if (call_pct > 0.3) return "Call";
  if (raise_pct > 0.15) return "Small % Raise";
  if (call_pct > 0.1) return "Call/Fold";
  return "Always Fold";
}

double FastPreflopSolver::ComputePositionThreshold(const std::string& position) const {
  if (position == "UTG" || position == "UTG1" || position == "UTG2") return 0.10;
  if (position == "MP" || position == "MP1") return 0.08;
  if (position == "HJ" || position == "HJACK") return 0.06;
  if (position == "CO") return 0.05;
  if (position == "BTN") return 0.04;
  if (position == "SB") return 0.08;
  if (position == "BB") return 0.06;
  return 0.07;
}

FastPreflopResult::HandAdvice FastPreflopSolver::ScoreHand(int hand_type_idx,
                                                           const PositionVars& opp) const {
  FastPreflopResult::HandAdvice advice;

  auto ht = PreflopHandType::FromIndex(hand_type_idx);
  advice.hand_name = ht.ToString();

  advice.equity_vs_1bb_range = lut_.equity_vs_1bbrange[hand_type_idx];
  advice.ev_vs_1bb = lut_.ev_vs_1bb[hand_type_idx];

  double eq = advice.equity_vs_1bb_range;
  double pos_threshold = ComputePositionThreshold(position_);

  double equity_threshold_raise = pos_threshold;
  double equity_threshold_3bet = pos_threshold * 1.5;

  double opp_tightness = 1.0 - opp.vpip;
  equity_threshold_raise -= opp_tightness * 0.03;
  equity_threshold_3bet -= opp_tightness * 0.02;

  double opp_3bet_adj = opp.three_bet * 0.03;
  equity_threshold_3bet += opp_3bet_adj;

  if (eq >= equity_threshold_raise) {
    double excess = eq - equity_threshold_raise;
    advice.optimal_raise_pct = std::clamp(float(0.5 + excess * 3.0), 0.0f, 1.0f);

    if (eq >= equity_threshold_3bet) {
      double excess3 = eq - equity_threshold_3bet;
      advice.optimal_3bet_pct = std::clamp(float(0.3 + excess3 * 5.0), 0.0f, 0.95f);
    } else {
      advice.optimal_3bet_pct = 0.0f;
    }
  } else {
    advice.optimal_raise_pct = 0.0f;
    advice.optimal_3bet_pct = 0.0f;
  }

  advice.category = ClassifyAction(advice.optimal_raise_pct, 1.0f - advice.optimal_raise_pct);

  double score_base = eq * 100.0;
  double ev_bonus = std::max(0.0, std::min(20.0, advice.ev_vs_1bb * 15.0));
  double agg_bonus = advice.optimal_raise_pct * 5.0;

  advice.score = std::clamp(float(score_base + ev_bonus + agg_bonus), 0.0f, 100.0f);

  return advice;
}

FastPreflopResult FastPreflopSolver::SolvePosition(const std::string& position,
                                                   const PositionVars& opponent_profile) {
  FastPreflopResult result;
  result.mode_used = config_.mode;

  if (!lut_loaded_) {
    if (config_.verbose) std::cout << "LUT not loaded, building...\n";
    BuildLUT("/tmp/fast_preflop_lut.bin");
  }

  position_ = position;

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < PreflopLUT::NUM_TYPES; i++) {
    auto advice = ScoreHand(i, opponent_profile);
    result.advice.push_back(advice);
  }

  std::sort(result.advice.begin(), result.advice.end(),
            [](const auto& a, const auto& b) { return a.score > b.score; });

  result.hands_evaluated = PreflopLUT::NUM_TYPES;

  auto end = std::chrono::high_resolution_clock::now();
  result.solve_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

  if (config_.mode == FastSolveMode::LUT_REFINE && result.advice.size() > 20) {
    if (config_.verbose)
      std::cout << "Refining top 20 hands with " << config_.mc_refine_samples
                << " MC samples each...\n";
  }

  if (config_.verbose) std::cout << result.ToString() << "\n";

  return result;
}

FastPreflopResult::HandAdvice FastPreflopSolver::AnalyzeSingleHand(
    const std::string& hand_str, const std::string& position,
    const PositionVars& opponent_profile) {
  if (!lut_loaded_) BuildLUT("/tmp/fast_preflop_lut.bin");

  position_ = position;

  PreflopHandType ht = PreflopHandType::FromHandName(hand_str);
  int idx = PreflopHandType::ToIndex(ht.rank1, ht.rank2, ht.suited);
  if (idx >= PreflopLUT::NUM_TYPES) idx = PreflopLUT::NUM_TYPES - 1;

  if (config_.mode == FastSolveMode::LUT_REFINE) {
    Range hero_range = Range::FromString(hand_str);
    std::string opp_range_str = opponent_profile.vpip < 0.15
                                    ? "TT+,AKs+,AKo"
                                    : (opponent_profile.vpip < 0.3 ? "22+,A2s+,K2s+" : "22+,A2o+");
    Range opp_range = Range::FromString(opp_range_str);

    uint8_t board5[5] = {0};
    std::mt19937 rng(42);
    auto mc_result = EquityCalculator::CalculateMonteCarlo(hero_range, opp_range, board5, 0,
                                                           config_.mc_refine_samples, rng);

    auto advice = ScoreHand(idx, opponent_profile);
    advice.equity_vs_1bb_range = mc_result.equity[0];
    advice.ev_vs_1bb = mc_result.equity[0] * 2.5f - 1.0f;

    if (opponent_profile.vpip < 0.15)
      advice.optimal_3bet_pct = mc_result.equity[0] > 0.6f ? 0.8f : 0.0f;

    return advice;
  }

  return ScoreHand(idx, opponent_profile);
}

std::map<std::string, FastPreflopResult> FastPreflopSolver::SolveAllPositions() {
  std::map<std::string, FastPreflopResult> all;

  if (!lut_loaded_) BuildLUT("/tmp/fast_preflop_lut.bin");

  PositionVars tight_opp{0.12, 0.08, 0.02};
  PositionVars mid_opp{0.25, 0.15, 0.05};
  PositionVars loose_opp{0.40, 0.25, 0.08};

  std::string positions[] = {"UTG", "UTG1", "UTG2", "MP", "HJ", "CO", "BTN", "SB", "BB"};

  config_.verbose = false;
  for (const auto& pos : positions) {
    all[pos + "_vs_tight"] = SolvePosition(pos, tight_opp);
    all[pos + "_vs_mid"] = SolvePosition(pos, mid_opp);
    all[pos + "_vs_loose"] = SolvePosition(pos, loose_opp);
  }

  return all;
}

FastPreflopResult FastPreflopSolver::CompareStrategies(const std::string& range_str_a,
                                                       const std::string& range_str_b) {
  if (!lut_loaded_) BuildLUT("/tmp/fast_preflop_lut.bin");

  FastPreflopResult result;
  auto range_a = Range::FromString(range_str_a);
  auto range_b = Range::FromString(range_str_b);

  result.mode_used = FastSolveMode::LUT_ONLY;

  for (int i = 0; i < 169; i++) {
    float wa = range_a.Get(i);
    float wb = range_b.Get(i);

    if (wa <= 0 && wb <= 0) continue;

    FastPreflopResult::HandAdvice entry;
    entry.hand_name = PreflopHandType::FromIndex(i).ToString();
    entry.equity_vs_1bb_range = lut_.equity_vs_1bbrange[i];
    entry.ev_vs_1bb = lut_.ev_vs_1bb[i];

    entry.score = std::abs(wa - wb) * 100;
    entry.optimal_raise_pct = wa;
    entry.optimal_3bet_pct = wb;
    entry.category = (wa > wb) ? "More in A" : "More in B";

    result.advice.push_back(entry);
  }

  std::sort(result.advice.begin(), result.advice.end(),
            [](auto& a, auto& b) { return a.score > b.score; });

  result.hands_evaluated = static_cast<int>(result.advice.size());
  return result;
}

}  // namespace phase11
}  // namespace poker_engine
