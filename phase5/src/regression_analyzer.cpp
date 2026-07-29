#include "poker_engine/phase5/regression_analyzer.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace poker_engine {
namespace phase5 {

// ===================== RegressionResult =====================

std::string RegressionResult::Interpret() const {
  if (n < 3) return "Insufficient data";
  std::string direction = slope > 0 ? "upward" : slope < 0 ? "downward" : "flat";
  std::string strength = std::abs(r_squared) > 0.7   ? "strong"
                         : std::abs(r_squared) > 0.3 ? "moderate"
                                                     : "weak";
  return strength + " " + direction + " trend (R²=" + std::to_string(int(r_squared * 100)) + "%)";
}

std::string RegressionResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(4);
  oss << "Slope: " << slope << "\n"
      << "Intercept: " << intercept << "\n"
      << "R²: " << r_squared << "\n"
      << "Std Error: " << std_error << "\n"
      << "N: " << n << "\n"
      << "Interpretation: " << Interpret() << "\n";
  return oss.str();
}

// ===================== WindowStats =====================

double WindowStats::Confidence95() const {
  if (count < 2) return 0;
  // t-value approximation for 95% CI (using 1.96 for large n)
  double t = count > 30 ? 1.96 : 2.0;
  return t * std_dev / std::sqrt(count);
}

std::string WindowStats::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "n=" << count << " mean=" << mean << " std=" << std_dev << " [" << min_val << ","
      << max_val << "]"
      << " CI95=±" << Confidence95();
  return oss.str();
}

// ===================== RegressionAnalyzer =====================

RegressionAnalyzer::RegressionAnalyzer() {}

void RegressionAnalyzer::AddPoint(double x, double y) {
  x_vals_.push_back(x);
  y_vals_.push_back(y);
}

void RegressionAnalyzer::AddProfit(double bb) {
  int idx = static_cast<int>(profits_.size()) + 1;
  AddPoint(idx, bb);
  profits_.push_back(bb);

  // Welford's online algorithm
  welford_count_++;
  double delta = bb - welford_mean_;
  welford_mean_ += delta / welford_count_;
  double delta2 = bb - welford_mean_;
  welford_m2_ += delta * delta2;
}

RegressionResult RegressionAnalyzer::LinearRegression() const {
  RegressionResult result;
  result.n = static_cast<int>(x_vals_.size());

  if (result.n < 2) return result;

  double sum_x = 0, sum_y = 0, sum_xx = 0, sum_xy = 0;
  for (int i = 0; i < result.n; i++) {
    sum_x += x_vals_[i];
    sum_y += y_vals_[i];
    sum_xx += x_vals_[i] * x_vals_[i];
    sum_xy += x_vals_[i] * y_vals_[i];
  }

  double denom = result.n * sum_xx - sum_x * sum_x;
  if (std::abs(denom) < 1e-10) return result;

  result.slope = (result.n * sum_xy - sum_x * sum_y) / denom;
  result.intercept = (sum_y - result.slope * sum_x) / result.n;

  // R²
  double mean_y = sum_y / result.n;
  double ss_tot = 0, ss_res = 0;
  for (int i = 0; i < result.n; i++) {
    double predicted = result.slope * x_vals_[i] + result.intercept;
    ss_res += (y_vals_[i] - predicted) * (y_vals_[i] - predicted);
    ss_tot += (y_vals_[i] - mean_y) * (y_vals_[i] - mean_y);
  }
  result.r_squared = ss_tot > 0 ? 1.0 - ss_res / ss_tot : 0;

  // Standard error
  if (result.n > 2) {
    result.std_error = std::sqrt(ss_res / (result.n - 2));
  }

  return result;
}

WindowStats RegressionAnalyzer::SlidingWindow(int window_size) const {
  WindowStats ws;

  int start = std::max(0, static_cast<int>(y_vals_.size()) - window_size);
  int end = static_cast<int>(y_vals_.size());
  int count = end - start;

  if (count <= 0) return ws;

  std::vector<double> window(y_vals_.begin() + start, y_vals_.end());
  std::sort(window.begin(), window.end());

  ws.count = count;
  ws.sum = std::accumulate(window.begin(), window.end(), 0.0);
  ws.mean = ws.sum / count;
  ws.min_val = window.front();
  ws.max_val = window.back();
  ws.median =
      (count % 2 == 0) ? (window[count / 2 - 1] + window[count / 2]) / 2.0 : window[count / 2];

  double variance = 0;
  for (double v : window) variance += (v - ws.mean) * (v - ws.mean);
  ws.variance = count > 1 ? variance / (count - 1) : 0;
  ws.std_dev = std::sqrt(ws.variance);

  return ws;
}

WindowStats RegressionAnalyzer::OverallStats() const {
  return SlidingWindow(static_cast<int>(y_vals_.size()));
}

std::vector<TrendPoint> RegressionAnalyzer::ComputeTrend(int window_size) const {
  std::vector<TrendPoint> trend;
  double cumulative = 0;

  for (int i = 0; i < static_cast<int>(profits_.size()); i++) {
    cumulative += profits_[i];

    TrendPoint pt;
    pt.hand_number = i + 1;
    pt.cumulative_profit = cumulative;
    pt.bb_per_100 = (i + 1) > 0 ? cumulative / (i + 1) * 100 : 0;

    // Running BB/100 with window
    int start = std::max(0, i + 1 - window_size);
    int window_len = i + 1 - start;
    double window_sum = 0;
    for (int j = start; j <= i; j++) window_sum += profits_[j];
    pt.running_bb_100 = window_len > 0 ? window_sum / window_len * 100 : 0;

    trend.push_back(pt);
  }

  return trend;
}

std::vector<int> RegressionAnalyzer::DetectOutliers(double threshold_sigma) const {
  std::vector<int> outliers;
  auto stats = OverallStats();

  for (int i = 0; i < static_cast<int>(y_vals_.size()); i++) {
    if (stats.std_dev > 0 && std::abs(y_vals_[i] - stats.mean) / stats.std_dev > threshold_sigma) {
      outliers.push_back(i);
    }
  }

  return outliers;
}

double RegressionAnalyzer::HeatIndex(int recent_hands) const {
  if (profits_.size() < 10) return 0.5;

  auto overall = OverallStats();
  auto recent = SlidingWindow(recent_hands);

  if (overall.std_dev <= 0) return 0.5;

  double z_score = (recent.mean - overall.mean) / overall.std_dev;
  // Normalize to 0~1 range
  return 1.0 / (1.0 + std::exp(-z_score));
}

std::vector<RegressionAnalyzer::Streak> RegressionAnalyzer::DetectStreaks() const {
  std::vector<Streak> streaks;
  if (profits_.empty()) return streaks;

  Streak current;
  current.start_idx = 0;
  current.length = 1;
  current.total_bb = profits_[0];
  current.is_winning = profits_[0] > 0;

  for (size_t i = 1; i < profits_.size(); i++) {
    bool this_winning = profits_[i] > 0;
    if (this_winning == current.is_winning) {
      current.length++;
      current.total_bb += profits_[i];
    } else {
      streaks.push_back(current);
      current.start_idx = static_cast<int>(i);
      current.length = 1;
      current.total_bb = profits_[i];
      current.is_winning = this_winning;
    }
  }
  streaks.push_back(current);

  return streaks;
}

void RegressionAnalyzer::Clear() {
  x_vals_.clear();
  y_vals_.clear();
  profits_.clear();
  welford_mean_ = 0;
  welford_m2_ = 0;
  welford_count_ = 0;
}

std::string RegressionAnalyzer::TrendReport(int window_size) const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);

  oss << "\n=== Regression Analysis Report ===\n\n";

  auto overall = OverallStats();
  oss << "Overall Statistics:\n" << overall.ToString() << "\n\n";

  auto recent = SlidingWindow(window_size);
  oss << "Last " << window_size << " Hands:\n" << recent.ToString() << "\n\n";

  auto reg = LinearRegression();
  oss << "Linear Trend:\n" << reg.ToString() << "\n";

  oss << "Heat Index: " << HeatIndex(window_size) << "\n";

  auto streaks = DetectStreaks();
  oss << "\nStreaks: " << streaks.size() << "\n";
  int max_win_streak = 0, max_loss_streak = 0;
  for (const auto& s : streaks) {
    if (s.is_winning && s.length > max_win_streak) max_win_streak = s.length;
    if (!s.is_winning && s.length > max_loss_streak) max_loss_streak = s.length;
  }
  oss << "  Longest win streak: " << max_win_streak << "\n";
  oss << "  Longest loss streak: " << max_loss_streak << "\n";

  auto outliers = DetectOutliers();
  oss << "\nOutliers (>2σ): " << outliers.size() << "\n";

  return oss.str();
}

}  // namespace phase5
}  // namespace poker_engine
