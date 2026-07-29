#include "poker_engine/anticheat/admin_handler.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream>

namespace poker_engine::anticheat {

std::string CaseRecord::Serialize() const {
  nlohmann::json j;
  j["case_id"] = case_id;
  j["player_id"] = player_id;
  j["player_name"] = player_name;
  j["alert_ids"] = alert_ids;
  j["confidence"] = confidence;
  j["status"] = status;
  j["assigned_to"] = assigned_to;
  j["created_at"] = created_at;
  j["closed_at"] = closed_at;
  j["notes"] = notes;
  return j.dump();
}

CaseRecord CaseRecord::Deserialize(const std::string& json_str) {
  CaseRecord rec;
  try {
    auto j = nlohmann::json::parse(json_str);
    rec.case_id = j.value("case_id", 0);
    rec.player_id = j.value("player_id", 0);
    rec.player_name = j.value("player_name", "");
    rec.confidence = j.value("confidence", 0.0f);
    rec.status = j.value("status", "open");
    rec.assigned_to = j.value("assigned_to", "");
    rec.created_at = j.value("created_at", 0ULL);
    rec.closed_at = j.value("closed_at", 0ULL);
    rec.notes = j.value("notes", "");
    if (j.contains("alert_ids")) rec.alert_ids = j["alert_ids"].get<std::vector<int64_t>>();
  } catch (...) {
  }
  return rec;
}

void CaseReviewQueue::AddCase(const CaseRecord& c) {
  queue_.push_back(c);
  Resort();
}
void CaseReviewQueue::UpdateCase(int64_t case_id, const CaseRecord& updated) {
  for (auto& c : queue_) {
    if (c.case_id == case_id) {
      c = updated;
      break;
    }
  }
  Resort();
}

CaseRecord CaseReviewQueue::GetNextCase() {
  if (queue_.empty()) return {};
  CaseRecord next = queue_.back();
  queue_.pop_back();
  return next;
}

std::vector<CaseRecord> CaseReviewQueue::GetPriorityQueue(int limit) {
  std::vector<CaseRecord> result;
  int count = std::min(limit, static_cast<int>(queue_.size()));
  for (int i = static_cast<int>(queue_.size()) - 1;
       i >= 0 && result.size() < static_cast<size_t>(count); --i)
    result.push_back(queue_[static_cast<size_t>(i)]);
  return result;
}

int CaseReviewQueue::PendingCount() const {
  return std::count_if(queue_.begin(), queue_.end(), [](const CaseRecord& c) {
    return c.status == "open" || c.status == "under_review";
  });
}
int CaseReviewQueue::TotalCount() const { return static_cast<int>(queue_.size()); }
void CaseReviewQueue::Resort() {
  std::sort(queue_.begin(), queue_.end(),
            [](const CaseRecord& a, const CaseRecord& b) { return a.confidence > b.confidence; });
}

}  // namespace poker_engine::anticheat
