#include "poker_engine/anticheat/anticheat_api.h"

#include <cmath>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace poker_engine::anticheat {

AntiCheatAPI::AntiCheatAPI(AntiCheatManager& manager) : manager_(manager) {}

std::string AntiCheatAPI::GetAlertsJSON(int limit, int offset) const {
  const auto& alerts = manager_.GetAlerts();
  size_t start = static_cast<size_t>(std::max(0, offset));
  size_t end = std::min(alerts.size(), start + static_cast<size_t>(limit));
  nlohmann::json j;
  j["total"] = alerts.size();
  j["limit"] = limit;
  j["offset"] = offset;
  nlohmann::json list = nlohmann::json::array();
  for (size_t i = start; i < end; ++i) {
    const auto& a = alerts[i];
    list.push_back({{"index", i},
                    {"player_id", a.player_id},
                    {"player_name", a.player_name},
                    {"level", SuspicionLevelName(a.level)},
                    {"reason", a.reason},
                    {"score", std::round(a.score * 10) / 10.0}});
  }
  j["alerts"] = list;
  return j.dump();
}

std::string AntiCheatAPI::GetPlayerReportJSON(int64_t player_id) const {
  const auto* stats = manager_.GetPlayerStats(player_id);
  double score = manager_.GetPlayerSuspicionScore(player_id);
  nlohmann::json j;
  if (stats) {
    j["player_id"] = stats->player_id;
    j["hands_played"] = stats->hands_played;
    j["hands_won"] = stats->hands_won;
    j["total_profit"] = stats->total_profit;
    j["vpip_pct"] = std::round(stats->vpip_pct * 100) / 100.0;
    j["pfr_pct"] = std::round(stats->pfr_pct * 100) / 100.0;
    j["agg_factor"] = std::round(stats->agg_factor * 100) / 100.0;
    j["three_bet_pct"] = std::round(stats->three_bet_pct * 100) / 100.0;
    j["positional_stats"] = {{"early", {{"hands", stats->early.hands}}},
                             {"middle", {{"hands", stats->middle.hands}}},
                             {"late", {{"hands", stats->late.hands}}},
                             {"blind", {{"hands", stats->blind.hands}}}};
    if (!stats->response_times_ms.empty()) {
      std::vector<double> rt_d(stats->response_times_ms.begin(), stats->response_times_ms.end());
      double mean_rt = PlayerStatistics::Mean(rt_d);
      double stddev_rt = PlayerStatistics::StdDev(rt_d, mean_rt);
      j["avg_response_ms"] = std::round(mean_rt * 100) / 100.0;
      j["response_stddev_ms"] = std::round(stddev_rt * 100) / 100.0;
    }
  }
  j["suspicion_score"] = std::round(score * 100) / 100.0;
  return j.dump();
}

std::string AntiCheatAPI::GetAlertDetailJSON(int64_t alert_index) const {
  const auto& alerts = manager_.GetAlerts();
  if (alert_index < 0 || static_cast<size_t>(alert_index) >= alerts.size())
    return "{\"error\":\"Alert not found\"}";
  const auto& a = alerts[static_cast<size_t>(alert_index)];
  nlohmann::json j;
  j["player_id"] = a.player_id;
  j["player_name"] = a.player_name;
  j["level"] = SuspicionLevelName(a.level);
  j["reason"] = a.reason;
  j["score"] = a.score;
  j["evidence"] = a.evidence;
  return j.dump();
}

std::string AntiCheatAPI::GetStatsJSON() const {
  nlohmann::json j;
  const auto& alerts = manager_.GetAlerts();
  int clean = 0, low = 0, medium = 0, high = 0, confirmed = 0;
  for (auto& a : alerts) {
    switch (a.level) {
      case SuspicionLevel::Clean:
        clean++;
        break;
      case SuspicionLevel::Low:
        low++;
        break;
      case SuspicionLevel::Medium:
        medium++;
        break;
      case SuspicionLevel::High:
        high++;
        break;
      case SuspicionLevel::Confirmed:
        confirmed++;
        break;
    }
  }
  j["total_alerts"] = alerts.size();
  j["by_level"] = {
      {"clean", clean}, {"low", low}, {"medium", medium}, {"high", high}, {"confirmed", confirmed}};
  return j.dump();
}

bool AntiCheatAPI::AcknowledgeAlert(int64_t alert_index) {
  return alert_index >= 0 && alert_index < static_cast<int64_t>(manager_.GetAlerts().size());
}
bool AntiCheatAPI::DismissAlert(int64_t alert_index) { return AcknowledgeAlert(alert_index); }

std::string AntiCheatAPI::ExportCSV() const {
  std::ostringstream oss;
  oss << "index,player_id,player_name,level,score,reason\n";
  int i = 0;
  for (auto& a : manager_.GetAlerts()) {
    oss << i++ << "," << a.player_id << ",\"" << EscapeJSON(a.player_name) << "\","
        << SuspicionLevelName(a.level) << "," << std::fixed << std::setprecision(1) << a.score
        << ",\"" << EscapeJSON(a.reason) << "\"\n";
  }
  return oss.str();
}

std::string AntiCheatAPI::SuspicionLevelName(SuspicionLevel level) {
  switch (level) {
    case SuspicionLevel::Clean:
      return "clean";
    case SuspicionLevel::Low:
      return "low";
    case SuspicionLevel::Medium:
      return "medium";
    case SuspicionLevel::High:
      return "high";
    case SuspicionLevel::Confirmed:
      return "confirmed";
  }
  return "unknown";
}

std::string AntiCheatAPI::EscapeJSON(const std::string& s) {
  std::ostringstream oss;
  for (char c : s) {
    switch (c) {
      case '"':
        oss << "\\\"";
        break;
      case '\\':
        oss << "\\\\";
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        oss << c;
    }
  }
  return oss.str();
}

}  // namespace poker_engine::anticheat
