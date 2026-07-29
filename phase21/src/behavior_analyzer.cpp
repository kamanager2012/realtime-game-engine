#include "poker_engine/security/behavior_analyzer.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <shared_mutex>

#include "poker_engine/base/logging.h"

namespace poker_engine::security {

namespace {

template <typename T>
T Mean(const std::deque<T>& v) {
  if (v.empty()) return 0;
  return std::accumulate(v.begin(), v.end(), T(0)) / v.size();
}

template <typename T>
T StdDev(const std::deque<T>& v) {
  if (v.size() < 2) return 0;
  T m = Mean(v);
  T sum_sq = 0;
  for (auto x : v) sum_sq += (x - m) * (x - m);
  return std::sqrt(sum_sq / (v.size() - 1));
}

double Skewness(const std::deque<double>& v) {
  if (v.size() < 3) return 0;
  double m = Mean(v);
  double s = StdDev(v);
  if (s < 1e-10) return 0;

  double sum = 0;
  for (auto x : v) sum += std::pow((x - m) / s, 3);
  return sum / v.size();
}

double Kurtosis(const std::deque<double>& v) {
  if (v.size() < 4) return 0;
  double m = Mean(v);
  double s = StdDev(v);
  if (s < 1e-10) return 0;

  double sum = 0;
  for (auto x : v) sum += std::pow((x - m) / s, 4);
  return (sum / v.size()) - 3.0;
}

double PearsonCorrelation(const std::deque<double>& x, const std::deque<double>& y) {
  if (x.size() != y.size() || x.size() < 3) return 0;

  double mx = Mean(x), my = Mean(y);
  double sx = StdDev(x), sy = StdDev(y);

  if (sx < 1e-10 || sy < 1e-10) return 0;

  double cov = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    cov += (x[i] - mx) * (y[i] - my);
  }
  return cov / ((x.size() - 1) * sx * sy);
}

}  // namespace

// ==================== BehavioralAnalysisEngine ====================

BehavioralAnalysisEngine::BehavioralAnalysisEngine(const Config& config) : config_(config) {}

void BehavioralAnalysisEngine::AddSample(int64_t player_id, const BehaviorSample& sample) {
  std::lock_guard<std::shared_mutex> lock(samples_mutex_);

  auto& deque = behavior_samples_[player_id];
  deque.push_back(sample);

  size_t max_samples = config_.min_samples_for_analysis * 10;
  if (deque.size() > max_samples) {
    deque.pop_front();
  }
}

std::optional<BehavioralProfile> BehavioralAnalysisEngine::GetProfile(int64_t player_id) const {
  std::shared_lock lock(samples_mutex_);

  auto it = behavior_samples_.find(player_id);
  if (it == behavior_samples_.end() ||
      it->second.size() < static_cast<size_t>(config_.min_samples_for_analysis)) {
    return std::nullopt;
  }

  return ComputeProfile(player_id, it->second);
}

double BehavioralAnalysisEngine::CompareBehavior(int64_t player_a, int64_t player_b) const {
  auto profile_a = GetProfile(player_a);
  auto profile_b = GetProfile(player_b);

  if (!profile_a || !profile_b) return 0.0;

  double similarity = 0.0;
  double weights = 0.0;

  double vpip_diff = std::abs(profile_a->vpip - profile_b->vpip);
  similarity += (1.0 - vpip_diff) * 0.2;
  weights += 0.2;

  double pfr_diff = std::abs(profile_a->pfr - profile_b->pfr);
  similarity += (1.0 - pfr_diff) * 0.2;
  weights += 0.2;

  double af_diff = std::abs(profile_a->agg_factor - profile_b->agg_factor) / 5.0;
  similarity += (1.0 - af_diff) * 0.15;
  weights += 0.15;

  double tb_diff = std::abs(profile_a->three_bet_pct - profile_b->three_bet_pct);
  similarity += (1.0 - tb_diff) * 0.15;
  weights += 0.15;

  double wtsd_diff = std::abs(profile_a->wtsd_pct - profile_b->wtsd_pct);
  similarity += (1.0 - wtsd_diff) * 0.15;
  weights += 0.15;

  double rt_diff = std::abs(profile_a->response_time_variance - profile_b->response_time_variance);
  similarity += (1.0 - rt_diff) * 0.15;
  weights += 0.15;

  return weights > 0 ? similarity / weights : 0.0;
}

double BehavioralAnalysisEngine::TimeCorrelation(int64_t player_a, int64_t player_b) const {
  std::shared_lock lock(samples_mutex_);

  auto it_a = behavior_samples_.find(player_a);
  auto it_b = behavior_samples_.find(player_b);

  if (it_a == behavior_samples_.end() || it_b == behavior_samples_.end()) return 0.0;

  std::deque<double> rt_a, rt_b;

  auto& sa = it_a->second;
  auto& sb = it_b->second;

  size_t min_size = std::min(sa.size(), sb.size());
  if (min_size < static_cast<size_t>(config_.min_samples_for_analysis)) return 0.0;

  for (size_t i = 0; i < min_size; ++i) {
    rt_a.push_back(sa[i].action_latency_ms);
    rt_b.push_back(sb[i].action_latency_ms);
  }

  return PearsonCorrelation(rt_a, rt_b);
}

double BehavioralAnalysisEngine::StrategyMirroring(int64_t player_a, int64_t player_b) const {
  std::shared_lock lock(samples_mutex_);

  auto it_a = behavior_samples_.find(player_a);
  auto it_b = behavior_samples_.find(player_b);

  if (it_a == behavior_samples_.end() || it_b == behavior_samples_.end()) return 0.0;

  size_t min_size = std::min(it_a->second.size(), it_b->second.size());
  if (min_size < static_cast<size_t>(config_.min_samples_for_analysis)) return 0.0;

  int same_actions = 0;
  size_t compared = 0;

  auto it_ai = it_a->second.begin();
  auto it_bi = it_b->second.begin();

  for (size_t i = 0; i < min_size; ++i, ++it_ai, ++it_bi) {
    if (it_ai->action_type == it_bi->action_type &&
        std::abs(it_ai->bet_ratio - it_bi->bet_ratio) < 0.1) {
      same_actions++;
    }
    compared++;
  }

  return compared > 0 ? static_cast<double>(same_actions) / compared : 0.0;
}

std::vector<BehavioralProfile> BehavioralAnalysisEngine::AnalyzeAll() {
  std::shared_lock lock(samples_mutex_);

  std::vector<BehavioralProfile> profiles;

  for (auto& [pid, samples] : behavior_samples_) {
    if (samples.size() >= static_cast<size_t>(config_.min_samples_for_analysis)) {
      profiles.push_back(ComputeProfile(pid, samples));
    }
  }

  return profiles;
}

BehavioralProfile BehavioralAnalysisEngine::ComputeProfile(
    int64_t player_id, const std::deque<BehaviorSample>& samples) const {
  BehavioralProfile profile;
  profile.player_id = player_id;
  profile.risk_score = 0.0;

  std::deque<double> response_times;
  for (auto& s : samples) {
    response_times.push_back(s.action_latency_ms);
  }

  profile.avg_response_time_ms = Mean(response_times);
  profile.response_time_variance = StdDev(response_times) * StdDev(response_times);
  profile.response_time_skewness = Skewness(response_times);
  profile.response_time_kurtosis = Kurtosis(response_times);

  int vpip_count = 0, pfr_count = 0;
  int three_bet_count = 0;
  int total_actions = 0;

  for (auto& s : samples) {
    if (s.action_type != 0) vpip_count++;
    if (s.action_type >= 2) pfr_count++;
    if (s.action_type == 3) three_bet_count++;
    total_actions++;
  }

  profile.vpip = total_actions > 0 ? static_cast<double>(vpip_count) / total_actions : 0;
  profile.pfr = total_actions > 0 ? static_cast<double>(pfr_count) / total_actions : 0;
  profile.agg_factor = profile.vpip > 0 ? profile.pfr / profile.vpip : 0;
  profile.three_bet_pct = profile.pfr > 0 ? static_cast<double>(three_bet_count) / profile.pfr : 0;

  profile.strategy_volatility = ComputeVolatility(samples);
  profile.position_awareness = ComputePositionAwareness(samples);

  double optimal_ratio = ComputeOptimalPlayRatio(samples);
  double consistency = ComputeConsistency(samples);

  profile.ComputeRiskScore();

  if (consistency < config_.consistency_threshold) {
    profile.risk_flags.push_back("CONSISTENT_RESPONSE_TIME");
  }

  if (optimal_ratio > config_.optimal_play_threshold) {
    profile.risk_flags.push_back("EXCESSIVELY_OPTIMAL");
  }

  if (profile.strategy_volatility < 0.05) {
    profile.risk_flags.push_back("ZERO_STRATEGY_VOLATILITY");
  }

  return profile;
}

double BehavioralAnalysisEngine::ComputeVolatility(
    const std::deque<BehaviorSample>& samples) const {
  int changes = 0;
  for (size_t i = 1; i < samples.size(); ++i) {
    if (samples[i].action_type != samples[i - 1].action_type) {
      changes++;
    }
  }
  return static_cast<double>(changes) / (samples.size() - 1);
}

double BehavioralAnalysisEngine::ComputePositionAwareness(
    const std::deque<BehaviorSample>& samples) const {
  std::unordered_map<int, std::vector<BehaviorSample>> by_position;
  for (auto& s : samples) {
    int pos = s.stack_at_action > 100 ? 0 : (s.stack_at_action > 50 ? 1 : 2);
    by_position[pos].push_back(s);
  }

  if (by_position.size() < 2) return 1.0;

  std::vector<double> vpips_by_pos;
  for (auto& [pos, pos_samples] : by_position) {
    int vpip = 0;
    for (auto& s : pos_samples) {
      if (s.action_type != 0) vpip++;
    }
    vpips_by_pos.push_back(static_cast<double>(vpip) / pos_samples.size());
  }

  double var = 0;
  if (vpips_by_pos.size() >= 2) {
    double m = Mean(std::deque<double>(vpips_by_pos.begin(), vpips_by_pos.end()));
    for (double v : vpips_by_pos) var += (v - m) * (v - m);
    var /= (vpips_by_pos.size() - 1);
  }

  return var;
}

double BehavioralAnalysisEngine::ComputeOptimalPlayRatio(
    const std::deque<BehaviorSample>& samples) const {
  double consistency = ComputeConsistency(samples);
  double variability = ComputeVolatility(samples);

  double score = consistency * (1.0 - variability) + (1.0 - consistency) * 0.5;

  return score;
}

double BehavioralAnalysisEngine::ComputeConsistency(
    const std::deque<BehaviorSample>& samples) const {
  std::deque<double> response_times;
  for (auto& s : samples) {
    response_times.push_back(s.action_latency_ms);
  }

  double mean = Mean(response_times);
  double stddev = StdDev(response_times);

  if (mean < 1e-10) return 1.0;

  double cv = stddev / mean;

  return std::max(0.0, 1.0 - cv / config_.consistency_threshold);
}

// ==================== BehavioralProfile ====================

void BehavioralProfile::ComputeRiskScore() {
  risk_score = 0.0;
  risk_flags.clear();

  double cv =
      std::sqrt(response_time_variance) / (avg_response_time_ms > 0 ? avg_response_time_ms : 1);
  if (cv < 0.1) {
    risk_score += 20;
    risk_flags.push_back("CONSISTENT_RESPONSE_TIME");
  }

  if (three_bet_pct > 0.8) {
    risk_score += 10;
    risk_flags.push_back("EXCESSIVE_3BET");
  }

  if (strategy_volatility < 0.05) {
    risk_score += 10;
    risk_flags.push_back("ZERO_VOLATILITY");
  }

  double optimal_ratio = pfr / (vpip > 0 ? vpip : 1);
  if (optimal_ratio > 0.85 && optimal_ratio < 0.95) {
    risk_score += 15;
    risk_flags.push_back("NEAR_OPTIMAL_RATIO");
  }

  if (std::abs(response_time_kurtosis) < 0.5) {
    risk_score += 10;
    risk_flags.push_back("ABNORMAL_KURTOSIS");
  }

  risk_score = std::min(100.0, risk_score);
}

}  // namespace poker_engine::security
