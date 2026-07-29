#pragma once

#include <optional>
#include <string>
#include <vector>

#include "poker_engine/anticheat/anticheat.h"

namespace poker_engine::anticheat {

class AntiCheatAPI {
 public:
  explicit AntiCheatAPI(AntiCheatManager& manager);

  std::string GetAlertsJSON(int limit = 50, int offset = 0) const;
  std::string GetPlayerReportJSON(int64_t player_id) const;
  std::string GetAlertDetailJSON(int64_t alert_index) const;
  std::string GetStatsJSON() const;

  bool AcknowledgeAlert(int64_t alert_index);
  bool DismissAlert(int64_t alert_index);

  std::string ExportCSV() const;

 private:
  AntiCheatManager& manager_;

  static std::string SuspicionLevelName(SuspicionLevel level);
  static std::string EscapeJSON(const std::string& s);
};

}  // namespace poker_engine::anticheat
