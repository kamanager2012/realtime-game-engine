#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace poker_engine::anticheat {

enum class AdminAction : uint8_t {
  ViewAlerts = 0,
  ViewPlayerReport = 1,
  AcknowledgeAlert = 2,
  DismissAlert = 3,
  BanPlayer = 4,
  MutePlayer = 5,
  ForceKick = 6,
  RunAnalysis = 7,
  ExportReport = 8,
};

struct AdminCommand {
  AdminAction action;
  int64_t target_id;
  std::string reason;
  std::string actor;
};

struct ReviewItem {
  int64_t alert_id;
  int64_t player_id;
  std::string player_name;
  std::string reason;
  double score;
  uint64_t timestamp;
  bool reviewed = false;
  std::string reviewer;
  std::string review_result;
};

struct CaseRecord {
  int64_t case_id;
  int64_t player_id;
  std::string player_name;
  std::vector<int64_t> alert_ids;
  float confidence;
  std::string status;
  std::string assigned_to;
  uint64_t created_at;
  uint64_t closed_at;
  std::string notes;

  std::string Serialize() const;
  static CaseRecord Deserialize(const std::string& json_str);
};

class CaseReviewQueue {
 public:
  void AddCase(const CaseRecord& c);
  void UpdateCase(int64_t case_id, const CaseRecord& updated);

  CaseRecord GetNextCase();
  std::vector<CaseRecord> GetPriorityQueue(int limit = 20);

  int PendingCount() const;
  int TotalCount() const;

  void Resort();

 private:
  std::vector<CaseRecord> queue_;
};

}  // namespace poker_engine::anticheat
