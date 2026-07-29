#include "poker_engine/phase5/equity_matrix.h"

#include <algorithm>
#include <iomanip>

#include "poker_engine/equity/equity_calculator.h"

namespace poker_engine {
namespace phase5 {

using namespace poker_engine::range;
using poker_engine::equity::EquityCalculator;

EquityMatrixCalculator::EquityMatrixCalculator() {}

EquityMatrixEntry EquityMatrixCalculator::SingleEquity(const Range& hero, const Range& villain,
                                                       const std::vector<poker_engine::Card>& board,
                                                       int n_samples) {
  uint8_t board5[5] = {0};
  int bs = 0;
  for (size_t i = 0; i < board.size() && i < 5; i++) {
    board5[i] = board[i].Id();
    bs++;
  }

  std::mt19937 rng(42);
  auto res = EquityCalculator::CalculateMonteCarlo(hero, villain, board5, bs, n_samples, rng);

  EquityMatrixEntry entry;
  entry.equity = res.equity[0];
  entry.tie_pct = res.tie[0] + res.tie[1];
  entry.trials = res.total_trials;
  return entry;
}

EquityMatrixResult EquityMatrixCalculator::Calculate(const std::vector<Range>& hero_ranges,
                                                     const std::vector<Range>& villain_ranges,
                                                     int n_samples) {
  EquityMatrixResult result;
  result.matrix.resize(hero_ranges.size());

  for (size_t i = 0; i < hero_ranges.size(); i++) {
    result.matrix[i].resize(villain_ranges.size());
    for (size_t j = 0; j < villain_ranges.size(); j++) {
      result.matrix[i][j] = SingleEquity(hero_ranges[i], villain_ranges[j], board_, n_samples);
    }
  }

  // Row averages
  result.row_avg_equity.resize(hero_ranges.size(), 0);
  for (size_t i = 0; i < hero_ranges.size(); i++) {
    double sum = 0;
    for (size_t j = 0; j < villain_ranges.size(); j++) sum += result.matrix[i][j].equity;
    result.row_avg_equity[i] = villain_ranges.size() > 0 ? sum / villain_ranges.size() : 0;
  }

  // Column averages
  result.col_avg_equity.resize(villain_ranges.size(), 0);
  for (size_t j = 0; j < villain_ranges.size(); j++) {
    double sum = 0;
    for (size_t i = 0; i < hero_ranges.size(); i++) sum += result.matrix[i][j].equity;
    result.col_avg_equity[j] = hero_ranges.size() > 0 ? sum / hero_ranges.size() : 0;
  }

  // Overall
  double total = 0;
  int count = 0;
  for (size_t i = 0; i < hero_ranges.size(); i++)
    for (size_t j = 0; j < villain_ranges.size(); j++) {
      total += result.matrix[i][j].equity;
      count++;
    }
  result.overall_avg_equity = count > 0 ? total / count : 0;

  return result;
}

EquityMatrixResult EquityMatrixCalculator::Calculate(
    const std::vector<std::string>& hero_range_strs,
    const std::vector<std::string>& villain_range_strs, int n_samples) {
  std::vector<Range> hero_ranges, villain_ranges;
  for (const auto& s : hero_range_strs) hero_ranges.push_back(Range::FromString(s));
  for (const auto& s : villain_range_strs) villain_ranges.push_back(Range::FromString(s));

  auto result = Calculate(hero_ranges, villain_ranges, n_samples);
  result.row_labels = hero_range_strs;
  result.col_labels = villain_range_strs;
  return result;
}

std::string EquityMatrixResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);

  // Header
  oss << std::setw(10) << " ";
  for (const auto& label : col_labels) oss << std::setw(8) << label.substr(0, 6);
  oss << "  Avg\n";

  oss << std::string(10 + col_labels.size() * 8 + 8, '-') << "\n";

  for (size_t i = 0; i < matrix.size(); i++) {
    oss << std::setw(10) << std::left << (row_labels.size() > i ? row_labels[i].substr(0, 8) : "?");
    for (size_t j = 0; j < matrix[i].size(); j++) {
      oss << std::setw(8) << std::right << int(matrix[i][j].equity * 100) << "%";
    }
    oss << "  " << int(row_avg_equity[i] * 100) << "%\n";
  }

  oss << "\nOverall avg equity: " << int(overall_avg_equity * 100) << "%\n";
  return oss.str();
}

std::string EquityMatrixResult::ToCSV() const {
  std::ostringstream oss;
  oss << "Hero\\Villain";
  for (const auto& label : col_labels) oss << "," << label;
  oss << ",Avg\n";

  for (size_t i = 0; i < matrix.size(); i++) {
    oss << (row_labels.size() > i ? row_labels[i] : "?");
    for (size_t j = 0; j < matrix[i].size(); j++) oss << "," << matrix[i][j].equity;
    oss << "," << row_avg_equity[i] << "\n";
  }
  return oss.str();
}

}  // namespace phase5
}  // namespace poker_engine
