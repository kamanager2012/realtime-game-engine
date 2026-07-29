#include "poker_engine/replay/replay_types.h"

#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

#include "poker_engine/base/serialization.h"

namespace poker_engine::replay {

std::string ReplayEvent::Serialize() const {
  nlohmann::json j;
  j["seq"] = sequence_id;
  j["type"] = static_cast<uint8_t>(type);
  j["hand_id"] = hand_id;
  j["player_id"] = player_id;
  j["timestamp"] = timestamp;
  j["details"] = details;
  return j.dump();
}

std::optional<ReplayEvent> ReplayEvent::Deserialize(const std::string& json_str) {
  try {
    auto j = nlohmann::json::parse(json_str);
    ReplayEvent evt;
    evt.sequence_id = j.value("seq", 0);
    evt.type = static_cast<ReplayEventType>(j.value("type", 0));
    evt.hand_id = j.value("hand_id", 0);
    evt.player_id = j.value("player_id", -1);
    evt.timestamp = j.value("timestamp", 0.0);
    evt.details = j.value("details", "");
    return evt;
  } catch (...) {
    return std::nullopt;
  }
}

std::string ReplayQuery::ToSQLWhere() const {
  std::ostringstream oss;
  std::string prefix = "WHERE ";
  if (player_id.has_value()) {
    oss << prefix << "hp.player_id = " << player_id.value();
    prefix = " AND ";
  }
  if (table_id.has_value()) {
    oss << prefix << "h.table_id = '" << table_id.value() << "'";
    prefix = " AND ";
  }
  if (hand_id.has_value()) {
    oss << prefix << "h.id = " << hand_id.value();
    prefix = " AND ";
  }
  if (start_time > 0) {
    oss << prefix << "h.start_time >= datetime(" << start_time << ")";
    prefix = " AND ";
  }
  if (end_time > 0) {
    oss << prefix << "h.end_time <= datetime(" << end_time << ")";
  }
  std::string result = oss.str();
  if (result.find("WHERE") == std::string::npos) return "";
  return result;
}

}  // namespace poker_engine::replay
