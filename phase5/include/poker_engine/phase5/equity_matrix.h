#pragma once
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase5 {

struct EquityMatrixEntry {
  double equity = 0;
  double tie_pct = 0;
  int64_t trials = 0;
};

struct EquityMatrixResult {
  std::vector<std::string> row_labels;
  std::vector<std::string> col_labels;
  std::vector<std::vector<EquityMatrixEntry>> matrix;
  std::vector<double> row_avg_equity;
  std::vector<double> col_avg_equity;
  double overall_avg_equity = 0;

  std::string ToString() const;
  std::string ToCSV() const;
};

class EquityMatrixCalculator {
 public:
  EquityMatrixCalculator();

  void SetSamples(int n) { n_samples_ = n; }
  void SetBoard(const std::vector<poker_engine::Card>& board) { board_ = board; }

  EquityMatrixResult Calculate(const std::vector<poker_engine::range::Range>& hero_ranges,
                               const std::vector<poker_engine::range::Range>& villain_ranges,
                               int n_samples = 5000);

  EquityMatrixResult Calculate(const std::vector<std::string>& hero_range_strs,
                               const std::vector<std::string>& villain_range_strs,
                               int n_samples = 5000);

  static EquityMatrixEntry SingleEquity(const poker_engine::range::Range& hero,
                                        const poker_engine::range::Range& villain,
                                        const std::vector<poker_engine::Card>& board,
                                        int n_samples = 10000);

 private:
  int n_samples_ = 5000;
  std::vector<poker_engine::Card> board_;
};

}  // namespace phase5
}  // namespace poker_engine
