#include "poker_engine/security/security_policy_engine.h"

#include <algorithm>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::security {

#define SECURITY_LOG(level, ...) PE_LOG_##level("[Security] " __VA_ARGS__)

SecurityPolicyEngine::SecurityPolicyEngine(const Config& config)
    : config_(config),
      ip_reputation_(),
      behavior_engine_(std::make_unique<BehavioralAnalysisEngine>()) {}

SecurityPolicyEngine::SecurityPolicyEngine() : SecurityPolicyEngine(Config()) {}

SecurityPolicyEngine::~SecurityPolicyEngine() = default;

void SecurityPolicyEngine::Initialize() {
  ip_reputation_.BulkImport("data/ip_reputation.csv");

  SECURITY_LOG(INFO, "Security policy engine initialized");
  SECURITY_LOG(INFO, "Config: auto_block={} auto_kick={} auto_flag={}",
               config_.auto_block_threshold, config_.auto_kick_threshold,
               config_.auto_flag_threshold);
}

RiskAssessment SecurityPolicyEngine::EvaluateConnection(
    const std::string& ip_address, const DeviceFingerprint& device,
    const anticheat::PlayerStatistics* existing_stats) {
  RiskAssessment assessment;
  assessment.player_id = device.ComputeHash();

  // 1. IP 信誉
  auto ip_rep = ip_reputation_.Query(ip_address);
  assessment.ip_score = 100.0 - ip_rep.RiskFactor();
  if (ip_rep.is_known_attacker) assessment.warnings.push_back("Known attacker IP");
  if (ip_rep.is_tor_exit) assessment.warnings.push_back("TOR exit node");
  if (ip_rep.is_proxy) assessment.warnings.push_back("Proxy detected");

  // 2. 设备指纹分析
  assessment.device_score = 85.0;

  // 3. 行为分析
  if (existing_stats) {
    anticheat::PlayerStatistics stats = *existing_stats;
    stats.ComputeRatios();

    double behavior_risk = 0.0;

    if (!stats.response_times_ms.empty()) {
      std::vector<double> rt_vec(stats.response_times_ms.begin(), stats.response_times_ms.end());
      double rt_mean = 0;
      for (auto v : rt_vec) rt_mean += v;
      rt_mean /= rt_vec.size();
      double rt_var = 0;
      for (auto v : rt_vec) rt_var += (v - rt_mean) * (v - rt_mean);
      rt_var /= (rt_vec.size() > 1 ? rt_vec.size() - 1 : 1);
      double cv = std::sqrt(rt_var) / (rt_mean + 1.0);

      if (cv < 0.1) behavior_risk += 20.0;
      if (cv < 0.05) behavior_risk += 15.0;
    }

    double ep_vpip = stats.early.hands > 0
                         ? stats.early.vpip / static_cast<double>(stats.early.hands) * 100.0
                         : 0;
    double lp_vpip =
        stats.late.hands > 0 ? stats.late.vpip / static_cast<double>(stats.late.hands) * 100.0 : 0;
    double vpip_var = std::abs(ep_vpip - lp_vpip);

    if (vpip_var < 2.0 && stats.hands_played > 50) {
      behavior_risk += 15.0;
      assessment.warnings.push_back("No positional awareness");
    }

    if (stats.vpip_pct > 0) {
      double pfr_ratio = stats.pfr_pct / stats.vpip_pct;
      if (pfr_ratio > 0.85 && pfr_ratio < 0.95) {
        behavior_risk += 10.0;
        assessment.warnings.push_back("Suspiciously optimal PFR/VPIP ratio");
      }
    }

    assessment.behavior_score = std::max(0.0, 100.0 - behavior_risk);
  } else {
    assessment.behavior_score = 50.0;
  }

  // 4. ML 反作弊评分
  assessment.ml_score = 50.0;

  // 5. 历史评分
  assessment.historical_score = 70.0;

  // 6. 加权综合
  assessment.Normalize();

  return assessment;
}

void SecurityPolicyEngine::EvaluateAction(int64_t player_id, const game::GameAction& action,
                                          const game::GameState& state) {
  if (!config_.enable_realtime_analysis) return;

  BehaviorSample sample;
  sample.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
  sample.action_type = static_cast<int>(action.type);
  sample.action_latency_ms = 0;
  sample.bet_ratio = state.GetPot() > 0 ? action.amount / state.GetPot() : 0;
  sample.pot_ratio = 0;
  sample.is_check = (action.type == game::ActionType::CHECK);
  sample.is_raise = (action.type == game::ActionType::RAISE);
  sample.stack_at_action = 0;

  behavior_engine_->AddSample(player_id, sample);

  auto profile = behavior_engine_->GetProfile(player_id);
  if (profile) {
    UpdateAssessment(player_id);

    if (profile->risk_score >= config_.auto_flag_threshold) {
      SecurityEvent event;
      event.event_id = event_log_.size() + 1;
      event.player_id = std::to_string(player_id);
      event.event_type = "behavior_analysis";
      event.risk_score = profile->risk_score;
      event.indicators = profile->risk_flags;
      event.auto_blocked = false;
      event.timestamp = std::chrono::system_clock::now();
      LogEvent(event);
    }
  }
}

std::vector<RiskAssessment> SecurityPolicyEngine::EvaluateAllPlayers() {
  auto profiles = behavior_engine_->AnalyzeAll();

  std::vector<RiskAssessment> results;
  results.reserve(profiles.size());

  for (auto& profile : profiles) {
    RiskAssessment assessment;
    assessment.player_id = std::to_string(profile.player_id);
    assessment.behavior_score = 100.0 - profile.risk_score;
    assessment.ml_score = 50.0;
    assessment.ip_score = 70.0;
    assessment.device_score = 80.0;
    assessment.historical_score = 70.0;
    assessment.warnings = profile.risk_flags;
    assessment.Normalize();

    results.push_back(assessment);
  }

  return results;
}

SecurityPolicyEngine::Decision SecurityPolicyEngine::MakeDecision(
    const RiskAssessment& assessment) {
  Decision decision;
  decision.confidence = assessment.confidence;

  int64_t pid = std::stoll(assessment.player_id);
  if (IsInCooldown(pid)) {
    decision.action = ActionType::Monitor;
    decision.reason = "Player in cooldown period";
    return decision;
  }

  double score = assessment.overall_score;

  if (score >= config_.auto_block_threshold) {
    decision.action = ActionType::Ban;
    decision.reason = "Risk score exceeds auto-block threshold (" + std::to_string(score) +
                      " >= " + std::to_string(config_.auto_block_threshold) + ")";
  } else if (score >= config_.auto_kick_threshold) {
    decision.action = ActionType::Kick;
    decision.reason = "Risk score exceeds auto-kick threshold (" + std::to_string(score) +
                      " >= " + std::to_string(config_.auto_kick_threshold) + ")";
  } else if (score >= config_.auto_flag_threshold) {
    decision.action = ActionType::Flag;
    decision.reason = "Risk score exceeds auto-flag threshold (" + std::to_string(score) +
                      " >= " + std::to_string(config_.auto_flag_threshold) + ")";
  } else {
    decision.action = ActionType::Allow;
    decision.reason = "Risk score within acceptable range";
  }

  decision.evidence = assessment.warnings;
  decision.evidence.insert(decision.evidence.end(), assessment.critical_flags.begin(),
                           assessment.critical_flags.end());

  return decision;
}

void SecurityPolicyEngine::ReportIPBehavior(const std::string& ip, ThreatLevel level,
                                            const std::string& reason) {
  ip_reputation_.ReportBehavior(ip, level, reason);
}

void SecurityPolicyEngine::IntegrateAntiCheatResult(const anticheat::CheatAlert& alert) {
  SecurityEvent event;
  event.event_id = event_log_.size() + 1;
  event.player_id = std::to_string(alert.player_id);
  event.event_type = "anticheat";
  event.risk_score = alert.score;
  event.indicators = {alert.reason};
  event.auto_blocked = (alert.level == anticheat::SuspicionLevel::Confirmed);
  event.timestamp = std::chrono::system_clock::now();
  LogEvent(event);

  if (alert.level == anticheat::SuspicionLevel::Confirmed) {
    auto decision = MakeDecision(GetPlayerRisk(alert.player_id));
    if (decision.action == ActionType::Ban) {
      SECURITY_LOG(WARN, "Auto-BAN player {}: {}", alert.player_id, alert.reason);
    }
  }
}

RiskAssessment SecurityPolicyEngine::GetPlayerRisk(int64_t player_id) const {
  std::shared_lock lock(assessments_mutex_);
  if (player_assessments_.count(player_id)) {
    return player_assessments_.at(player_id);
  }

  RiskAssessment default_assessment;
  default_assessment.player_id = std::to_string(player_id);
  default_assessment.ip_score = 70.0;
  default_assessment.device_score = 80.0;
  default_assessment.behavior_score = 70.0;
  default_assessment.ml_score = 50.0;
  default_assessment.historical_score = 70.0;
  default_assessment.confidence = 0.2;
  default_assessment.Normalize();
  return default_assessment;
}

// ==================== 内部方法 ====================

void SecurityPolicyEngine::UpdateAssessment(int64_t player_id) {
  std::lock_guard<std::shared_mutex> lock(assessments_mutex_);

  auto& assessment = player_assessments_[player_id];
  assessment.player_id = std::to_string(player_id);

  auto profile = behavior_engine_->GetProfile(player_id);
  if (profile) {
    assessment.behavior_score = 100.0 - profile->risk_score;

    for (auto& flag : profile->risk_flags) {
      if (std::find(assessment.warnings.begin(), assessment.warnings.end(), flag) ==
          assessment.warnings.end()) {
        assessment.warnings.push_back(flag);
      }
    }
  }

  assessment.Normalize();
}

bool SecurityPolicyEngine::IsPlayerFlagged(int64_t player_id) const {
  std::shared_lock lock(assessments_mutex_);
  auto it = player_assessments_.find(player_id);
  if (it == player_assessments_.end()) return false;
  return it->second.GetLevel() != RiskAssessment::RiskLevel::Safe;
}

std::vector<uint64_t> SecurityPolicyEngine::GetActiveIncidents() const {
  std::lock_guard<std::shared_mutex> lock(event_mutex_);

  std::vector<uint64_t> incidents;
  for (auto& event : event_log_) {
    if (event.risk_score >= config_.auto_flag_threshold) {
      incidents.push_back(event.event_id);
    }
  }
  return incidents;
}

typename SecurityPolicyEngine::Stats SecurityPolicyEngine::GetStats() const {
  Stats s = stats_;
  s.active_incidents = static_cast<int>(GetActiveIncidents().size());
  return s;
}

bool SecurityPolicyEngine::IsInCooldown(int64_t player_id) const {
  std::shared_lock lock(assessments_mutex_);
  auto it = ban_cooldown_.find(player_id);
  if (it == ban_cooldown_.end()) return false;

  auto now = std::chrono::system_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();

  return elapsed < config_.ban_cooldown_seconds;
}

void SecurityPolicyEngine::LogEvent(const SecurityEvent& event) {
  std::lock_guard<std::shared_mutex> lock(event_mutex_);
  event_log_.push_back(event);

  if (event_log_.size() > 10000) {
    event_log_.erase(event_log_.begin(), event_log_.begin() + event_log_.size() / 4);
  }
}

std::string SecurityPolicyEngine::GenerateIncidentReport(const RiskAssessment& assessment) const {
  nlohmann::json report;
  report["player_id"] = assessment.player_id;
  report["overall_score"] = assessment.overall_score;
  report["confidence"] = assessment.confidence;
  report["risk_level"] = assessment.LevelToString();
  report["breakdown"] = {{"ip_score", assessment.ip_score},
                         {"device_score", assessment.device_score},
                         {"behavior_score", assessment.behavior_score},
                         {"ml_score", assessment.ml_score},
                         {"historical_score", assessment.historical_score}};
  report["warnings"] = assessment.warnings;
  report["critical_flags"] = assessment.critical_flags;
  report["generated_at"] = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

  return report.dump(2);
}

std::string SecurityPolicyEngine::ExportSecurityReport() const {
  nlohmann::json report;

  report["config"] = {{"auto_block_threshold", config_.auto_block_threshold},
                      {"auto_kick_threshold", config_.auto_kick_threshold},
                      {"auto_flag_threshold", config_.auto_flag_threshold}};

  report["stats"] = {{"total_players_evaluated", stats_.total_players_evaluated},
                     {"auto_blocks", stats_.auto_blocks},
                     {"manual_reviews", stats_.manual_reviews},
                     {"avg_risk_score", stats_.avg_risk_score},
                     {"active_incidents", stats_.active_incidents}};

  nlohmann::json incidents_json = nlohmann::json::array();
  std::lock_guard<std::shared_mutex> lock(event_mutex_);
  for (auto& event : event_log_) {
    if (event.risk_score >= config_.auto_flag_threshold) {
      incidents_json.push_back({{"id", event.event_id},
                                {"player", event.player_id},
                                {"type", event.event_type},
                                {"score", event.risk_score},
                                {"auto_blocked", event.auto_blocked},
                                {"indicators", event.indicators}});
    }
  }
  report["active_incidents"] = incidents_json;

  return report.dump(2);
}

}  // namespace poker_engine::security
