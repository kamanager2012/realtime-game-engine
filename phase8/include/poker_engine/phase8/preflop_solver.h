#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase8 {

enum PreflopPosition { UTG = 0, UTG1, UTG2, MP, MP1, HJ, CO, BTN, SB, BB, NUM_POSITIONS };

static const std::string PositionName[] = {"UTG", "UTG1", "UTG2", "MP", "MP1",
                                           "HJ",  "CO",   "BTN",  "SB", "BB"};

struct PreflopConfig {
  int iterations = 1000;
  int mc_samples = 2000;
  double convergence_threshold = 0.001;
  bool use_pot_odds = true;
  std::vector<PreflopPosition> positions;
  std::vector<double> stacks;
  double blinds[3] = {0.5, 1.0, 0};
  bool verbose = false;
};

struct PreflopHandInfo {
  uint8_t card1 = 0xFF, card2 = 0xFF;
  bool suited = false;
  std::string hand_name;
  double equity_vs_1bbs[NUM_POSITIONS] = {};
  double equity_vs_3bbs[NUM_POSITIONS] = {};
  std::string ToString() const;
};

struct PreflopAdvice {
  PreflopPosition position;
  std::string hand_name;
  std::string recommended_action;
  double raise_freq = 0;
  double call_freq = 0;
  double fold_freq = 0;
  double min_raise_pct = 0;
  double min_3bet_pct = 0;
  double ev_vs_1bb = 0;
  double ev_vs_3bb = 0;
  std::string ToString() const;
};

struct PreflopMatrix169 {
  static constexpr int NUM_TYPES = 169;
  std::array<std::array<double, NUM_TYPES>, NUM_TYPES> equity_matrix;
  std::array<std::string, NUM_TYPES> labels;
  void Init();
  double GetEquity(int hero_idx, int villain_idx, int street) const;
};

class PreflopSolver {
 public:
  explicit PreflopSolver(const PreflopConfig& config = PreflopConfig());
  void Init(const PreflopConfig& config);

  std::vector<PreflopAdvice> SolvePosition(PreflopPosition pos);
  std::map<PreflopPosition, std::vector<PreflopAdvice>> SolveAll();
  PreflopHandInfo AnalyzeHand(const std::string& hand_str, PreflopPosition pos);
  PreflopMatrix169 GetEquityMatrix();

  static std::vector<PreflopAdvice> QuickSolve(PreflopPosition pos, int n_samples = 5000);

 private:
  PreflopConfig config_;
  PreflopMatrix169 equity_matrix_;
  std::mt19937 rng_{42};

  void ComputeAllEquities();
  double ComputeEquityVsRange(const poker_engine::range::Range& hand,
                              const poker_engine::range::Range& range, int n_samples);
  static int HandToIndex(uint8_t c1, uint8_t c2);
  static bool IsSuited(uint8_t c1, uint8_t c2) { return (c1 % 4) == (c2 % 4); }
};

}  // namespace phase8
}  // namespace poker_engine
