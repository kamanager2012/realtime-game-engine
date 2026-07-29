#pragma once
#include <array>
#include <cstdint>
#include <random>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace equity {

using poker_engine::range::Range;

struct EquityResult {
  float win[2] = {0, 0};
  float tie[2] = {0, 0};
  float equity[2] = {0, 0};
  uint64_t total_trials = 0;
  std::string ToString() const;
};

class EquityCalculator {
 public:
  static EquityResult CalculateMonteCarlo(const Range& r1, const Range& r2, const uint8_t board[],
                                          int board_size, int n_samples, std::mt19937& rng);

  static EquityResult CalculateExact(const Range& r1, const Range& r2, const uint8_t board[],
                                     int board_size, int samples = -1);

  static EquityResult CalculatePreflopMC(int n_samples, std::mt19937& rng);

 private:
  static int EvaluateHeadToHead(uint8_t c1a, uint8_t c2a, uint8_t c1b, uint8_t c2b,
                                const uint8_t board[], int board_size);
  static void DrawBoard(uint8_t board_out[], const uint8_t partial[], int partial_size,
                        uint8_t used[52], int target_size, std::mt19937& rng);
};

}  // namespace equity
}  // namespace poker_engine
