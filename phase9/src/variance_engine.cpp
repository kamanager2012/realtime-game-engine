#include "poker_engine/phase9/variance_engine.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace poker_engine {
namespace phase9 {

static double TQuantile(double p, int df) {
  if (df <= 0) return 0;
  // Approximate the t-distribution quantile using Abramowitz & Stegun
  // Input p = cumulative probability (0 < p < 1)
  // We want x such that P(T <= x) = p
  if (p <= 0.5) return -TQuantile(1.0 - p, df);
  double u = 1.0 - p;
  double y = sqrt(-2.0 * log(u));
  double a0 = 2.515517, a1 = 0.802853, a2 = 0.010328;
  double b1 = 1.432788, b2 = 0.189269, b3 = 0.001308;
  double x = y - (a0 + a1 * y + a2 * y * y) / (1 + b1 * y + b2 * y * y + b3 * y * y * y);
  // Cornish-Fisher corrections for t-distribution
  double g1 = (x * x * x + x) / (4.0 * df);
  double g2 = (5 * x * x * x * x * x + 16 * x * x * x + 3 * x) / (96.0 * df * df);
  return x + g1 + g2;
}

std::string StatisticalSummary::ToString() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(2);
  o << "=== Statistical Summary ===\n";
  o << "Samples: " << n << " | Mean: " << mean << " BB/100 | Median: " << median << "\n";
  o << "StdDev: " << std_dev << " | StdErr: " << stderr << " | Min: " << min_val
    << " | Max: " << max_val << "\n";
  o << "Skew: " << skewness << " | Kurt: " << kurtosis << "\n";
  o << "90% CI: [" << ci_90_low << ", " << ci_90_high << "]\n";
  o << "95% CI: [" << ci_95_low << ", " << ci_95_high << "]\n";
  o << "99% CI: [" << ci_99_low << ", " << ci_99_high << "]\n";
  o << "Win%: " << win_rate_pct << " | Adj BB/100: " << bb_per_100_adjusted << "\n";
  return o.str();
}

std::string SWRRResult::ToString() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(2);
  o << "=== SWRR ===\n";
  o << "SWRR: " << swrr << " | BB/100: " << bb_per_100 << " | Confidence: " << confidence * 100
    << "%\n";
  o << "N: " << sample_size << " | Class: " << classification << "\n";
  o << "RoR: " << risk_of_ruin * 100 << "% | CE: [" << certain_equity_lower << ", "
    << certain_equity_upper << "]\n";
  return o.str();
}

std::string HeatAnalysis::ToString() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(2);
  o << "=== Heat ===\n";
  o << "Index: " << heat_index << "σ | Recent: " << recent_bb100 << " | Overall: " << overall_bb100
    << "\n";
  o << "P: " << p_value << " | " << verdict << "\n" << advice << "\n";
  return o.str();
}

std::string ComparisonResult::ToString() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(4);
  o << "=== Period Comparison ===\n";
  o << "A(" << period_a << "): " << mean_a << " | B(" << period_b << "): " << mean_b << "\n";
  o << "Diff: " << diff << " | P: " << p_value << " | Sig: " << (significant ? "YES" : "NO")
    << "\n";
  o << interpretation << "\n";
  return o.str();
}

VarianceEngine::VarianceEngine() = default;
void VarianceEngine::AddSample(const SessionSample& s) { samples_.push_back(s); }
void VarianceEngine::AddSamples(const std::vector<SessionSample>& v) {
  for (auto& s : v) AddSample(s);
}
void VarianceEngine::AddBB100(double bb, int hands, const std::string& date) {
  SessionSample s;
  s.session_id = Count() + 1;
  s.hands_played = hands;
  s.bb_per_100 = bb;
  s.net_result = bb * hands / 100.0;
  s.std_dev = std::abs(bb) + 10;
  s.date = date;
  samples_.push_back(s);
}
double VarianceEngine::TValue(double conf, int df) const {
  return TQuantile(1.0 - (1.0 - conf) / 2.0, df > 0 ? df : 1);
}
double VarianceEngine::NormalCDF(double z) const {
  return 0.5 * (1.0 + std::erf(z / std::sqrt(2.0)));
}

StatisticalSummary VarianceEngine::ComputeSummary() const { return ComputeSummaryWindow(0); }

StatisticalSummary VarianceEngine::ComputeSummaryWindow(int last_n) const {
  StatisticalSummary ss;
  int start = 0;
  if (last_n > 0 && last_n < Count()) start = Count() - last_n;
  int n = Count() - start;
  ss.n = n;
  if (n == 0) return ss;

  std::vector<double> v(n);
  for (int i = 0; i < n; i++) v[i] = samples_[start + i].bb_per_100;

  ss.mean = std::accumulate(v.begin(), v.end(), 0.0) / n;
  double sq = 0;
  for (double x : v) sq += (x - ss.mean) * (x - ss.mean);
  ss.variance = n > 1 ? sq / (n - 1) : 0;
  ss.std_dev = std::sqrt(ss.variance);
  ss.stderr = n > 1 ? ss.std_dev / std::sqrt(n) : 0;

  auto sorted = v;
  std::sort(sorted.begin(), sorted.end());
  ss.median = n % 2 == 0 ? (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0 : sorted[n / 2];

  if (ss.variance > 0) {
    double m3 = 0, m4 = 0;
    for (double x : v) {
      double z = (x - ss.mean) / ss.std_dev;
      m3 += z * z * z;
      m4 += z * z * z * z;
    }
    ss.skewness = n > 2 ? m3 / n : 0;
    ss.kurtosis = n > 3 ? m4 / n - 3.0 : 0;
  }
  ss.min_val = *std::min_element(v.begin(), v.end());
  ss.max_val = *std::max_element(v.begin(), v.end());

  if (n > 1) {
    double t90 = TValue(0.90, n - 1), t95 = TValue(0.95, n - 1), t99 = TValue(0.99, n - 1);
    ss.ci_90_low = ss.mean - t90 * ss.stderr;
    ss.ci_90_high = ss.mean + t90 * ss.stderr;
    ss.ci_95_low = ss.mean - t95 * ss.stderr;
    ss.ci_95_high = ss.mean + t95 * ss.stderr;
    ss.ci_99_low = ss.mean - t99 * ss.stderr;
    ss.ci_99_high = ss.mean + t99 * ss.stderr;
  }
  ss.win_rate_pct = 50 + ss.mean / 2.0;
  return ss;
}

SWRRResult VarianceEngine::ComputeSWRR() const { return ComputeSWRRWindow(0); }

SWRRResult VarianceEngine::ComputeSWRRWindow(int last_n) const {
  SWRRResult r;
  auto ss = ComputeSummaryWindow(last_n);
  r.bb_per_100 = ss.mean;
  r.sample_size = ss.n;
  r.swrr = ss.std_dev > 0 ? std::clamp(ss.mean / ss.std_dev, -2.0, 2.0) : 0;
  if (ss.n > 0 && ss.std_dev > 0) {
    double w = ss.ci_95_high - ss.ci_95_low;
    r.confidence = std::clamp(1.0 - w / std::max(std::abs(ss.mean), 1.0), 0.0, 1.0);
  }
  if (ss.mean > 10)
    r.classification = "Highly Profitable";
  else if (ss.mean > 4)
    r.classification = "Profitable";
  else if (ss.mean > 0)
    r.classification = "Slightly +EV";
  else if (ss.mean > -4)
    r.classification = "Slightly -EV";
  else
    r.classification = "Unprofitable";

  double br = ss.n * 100;
  r.risk_of_ruin =
      ss.mean > 0 ? std::clamp(std::exp(-2.0 * ss.mean * br / (ss.std_dev * ss.std_dev)), 0.0, 1.0)
                  : 1.0;
  r.certain_equity_lower = ss.ci_95_low;
  r.certain_equity_upper = ss.ci_95_high;
  return r;
}

HeatAnalysis VarianceEngine::ComputeHeatIndex(int last_n) const {
  HeatAnalysis h;
  int n = Count();
  if (n < 10) {
    h.verdict = "INSUFFICIENT";
    h.advice = "Collect more data";
    return h;
  }
  auto all = ComputeSummaryWindow(0), recent = ComputeSummaryWindow(last_n);
  h.overall_bb100 = all.mean;
  h.recent_bb100 = recent.mean;
  if (all.std_dev > 0) h.heat_index = (recent.mean - all.mean) / all.std_dev;
  double se = all.std_dev / std::sqrt(recent.n);
  if (se > 0) {
    double z = std::abs(recent.mean - all.mean) / se;
    h.p_value = 2 * (1 - NormalCDF(z));
  }
  if (h.heat_index > 1.5) {
    h.verdict = "HOT";
    h.advice = "Running above EV, may regress.";
  } else if (h.heat_index < -1.5) {
    h.verdict = "COLD";
    h.advice = "Running below EV, review strategy.";
  } else {
    h.verdict = "NORMAL";
    h.advice = "Within expected variance.";
  }
  return h;
}

std::pair<double, double> VarianceEngine::ConfidenceInterval(double conf) const {
  auto ss = ComputeSummary();
  if (ss.n < 2) return {ss.mean, ss.mean};
  double m = TValue(conf, ss.n - 1) * ss.stderr;
  return {ss.mean - m, ss.mean + m};
}

double VarianceEngine::RiskOfRuin(double bankroll, double buyin) const {
  auto ss = ComputeSummary();
  if (ss.n < 1 || buyin <= 0 || bankroll <= 0) return 0;
  // ss.mean is in BB/100, convert to win rate per hand in BB
  double wr_per_hand = ss.mean / 100.0;  // BB per hand
  if (wr_per_hand <= 0) return 1.0;      // Negative or zero win rate = certain ruin
  if (ss.std_dev <= 0) return 0;         // Positive win rate with zero variance = no ruin
  // Variance per hand in BB^2
  double var_per_hand = (ss.std_dev * ss.std_dev) / 100.0;
  // Classic risk of ruin formula: RoR = exp(-2 * wr * bankroll / variance)
  double bankroll_bb = bankroll / buyin;
  double ror = std::exp(-2.0 * wr_per_hand * bankroll_bb / var_per_hand);
  return std::clamp(ror, 0.0, 1.0);
}

double VarianceEngine::CertainEquilibrium(double, double conf) const {
  auto ss = ComputeSummary();
  if (ss.n < 1) return 0;
  double z = TQuantile(1.0 - (1.0 - conf) / 2.0, ss.n - 1);
  return ss.mean - z * ss.std_dev / std::sqrt(ss.n);
}

std::vector<std::pair<int, double>> VarianceEngine::RunningVariance(int w) const {
  std::vector<std::pair<int, double>> r;
  if (Count() < w) return r;
  for (int i = w - 1; i < Count(); i++) {
    double s = 0, sq = 0;
    for (int j = i - w + 1; j <= i; j++) {
      s += samples_[j].bb_per_100;
      sq += samples_[j].bb_per_100 * samples_[j].bb_per_100;
    }
    double m = s / w;
    r.push_back({i + 1, sq / w - m * m});
  }
  return r;
}

std::vector<std::pair<int, double>> VarianceEngine::RunningWinRate(int w) const {
  std::vector<std::pair<int, double>> r;
  if (Count() < w) return r;
  for (int i = w - 1; i < Count(); i++) {
    double s = 0;
    for (int j = i - w + 1; j <= i; j++) s += samples_[j].bb_per_100;
    r.push_back({i + 1, s / w});
  }
  return r;
}

void VarianceEngine::Clear() { samples_.clear(); }

ComparisonResult ComparePeriods(const std::vector<SessionSample>& pa,
                                const std::vector<SessionSample>& pb) {
  ComparisonResult r;
  if (pa.empty() || pb.empty()) {
    r.interpretation = "Cannot compare: empty";
    return r;
  }
  auto mean = [](const std::vector<SessionSample>& s) {
    double m = 0;
    for (auto& x : s) m += x.bb_per_100;
    return m / s.size();
  };
  auto vari = [](const std::vector<SessionSample>& s, double m) {
    double v = 0;
    for (auto& x : s) v += (x.bb_per_100 - m) * (x.bb_per_100 - m);
    return s.size() > 1 ? v / (s.size() - 1) : 0;
  };
  double na = pa.size(), nb = pb.size(), ma = mean(pa), mb = mean(pb), va = vari(pa, ma),
         vb = vari(pb, mb);
  r.period_a = "A(" + std::to_string(pa.size()) + ")";
  r.period_b = "B(" + std::to_string(pb.size()) + ")";
  r.mean_a = ma;
  r.mean_b = mb;
  r.diff = ma - mb;
  double se = std::sqrt(va / na + vb / nb);
  if (se > 0) {
    double t = (ma - mb) / se;
    double z = std::abs(t);
    r.p_value = 2 * (1 - 0.5 * (1 + std::erf(z / std::sqrt(2))));
    r.significant = r.p_value < 0.05;
  }
  if (std::abs(r.diff) < 1)
    r.interpretation = "Equivalent.";
  else
    r.interpretation = std::string(r.diff > 0 ? "A > B" : "B > A") + " by " +
                       std::to_string(int(std::abs(r.diff) + 0.5)) + " BB/100" +
                       (r.significant ? " (sig)" : "");
  return r;
}

}  // namespace phase9
}  // namespace poker_engine
