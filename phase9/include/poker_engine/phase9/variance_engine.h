#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <string>
#include <vector>

namespace poker_engine {
namespace phase9 {

struct SessionSample {
  int session_id = 0;
  int hands_played = 0;
  double bb_per_100 = 0;
  double net_result = 0;
  double std_dev = 0;
  std::string date = "";
};

struct StatisticalSummary {
  int n = 0;
  double mean = 0;
  double variance = 0;
  double std_dev = 0;
  double stderr = 0;
  double median = 0;
  double skewness = 0;
  double kurtosis = 0;
  double min_val = 0;
  double max_val = 0;
  double ci_90_low = 0;
  double ci_90_high = 0;
  double ci_95_low = 0;
  double ci_95_high = 0;
  double ci_99_low = 0;
  double ci_99_high = 0;
  double bb_per_100_adjusted = 0;
  double win_rate_pct = 0;
  std::string ToString() const;
};

struct SWRRResult {
  double swrr = 0;
  double bb_per_100 = 0;
  double confidence = 0;
  int sample_size = 0;
  std::string classification;
  double risk_of_ruin = 0;
  double certain_equity_lower = 0;
  double certain_equity_upper = 0;
  std::string ToString() const;
};

struct HeatAnalysis {
  double heat_index = 0;
  double recent_bb100 = 0;
  double overall_bb100 = 0;
  double p_value = 0;
  std::string verdict;
  std::string advice;
  std::string ToString() const;
};

class VarianceEngine {
 public:
  VarianceEngine();
  void AddSample(const SessionSample& sample);
  void AddSamples(const std::vector<SessionSample>& samples);
  void AddBB100(double bb_per_100, int hands = 100, const std::string& date = "");

  StatisticalSummary ComputeSummary() const;
  StatisticalSummary ComputeSummaryWindow(int last_n_sessions) const;

  SWRRResult ComputeSWRR() const;
  SWRRResult ComputeSWRRWindow(int last_n_sessions) const;

  HeatAnalysis ComputeHeatIndex(int last_n_sessions = 50) const;

  std::pair<double, double> ConfidenceInterval(double confidence_level = 0.95) const;

  double RiskOfRuin(double bankroll, double avg_buyin) const;
  double CertainEquilibrium(double bankroll, double confidence = 0.95) const;

  std::vector<std::pair<int, double>> RunningVariance(int window = 50) const;
  std::vector<std::pair<int, double>> RunningWinRate(int window = 50) const;

  void Clear();
  int Count() const { return static_cast<int>(samples_.size()); }
  const std::vector<SessionSample>& Samples() const { return samples_; }

 private:
  std::vector<SessionSample> samples_;
  double TValue(double confidence, int df) const;
  double NormalCDF(double z) const;
};

struct ComparisonResult {
  std::string period_a;
  std::string period_b;
  double mean_a = 0;
  double mean_b = 0;
  double diff = 0;
  double p_value = 0;
  bool significant = false;
  std::string interpretation;
  std::string ToString() const;
};

ComparisonResult ComparePeriods(const std::vector<SessionSample>& period_a,
                                const std::vector<SessionSample>& period_b);

}  // namespace phase9
}  // namespace poker_engine
