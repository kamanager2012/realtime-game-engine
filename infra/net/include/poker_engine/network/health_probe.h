#pragma once

#include <cstdint>
#include <sstream>
#include <string>

namespace poker_engine::network {

// Shared liveness/readiness payload for HTTP GET /health (poker_ws_server + infra/net).
struct HealthSnapshot {
  bool production_mode = false;
  bool db_healthy = false;
  int table_count = 0;
  bool redis_healthy = false;
  bool redis_required = false;
  std::string instance_id;
  bool postgres_healthy = false;
};

inline std::string BuildHealthJson(const HealthSnapshot& snapshot) {
  std::ostringstream json;
  json << "{\"status\":\"ok\"";
  json << ",\"mode\":\"" << (snapshot.production_mode ? "production" : "development") << "\"";
  json << ",\"db\":" << (snapshot.db_healthy ? "true" : "false");
  json << ",\"tables\":" << snapshot.table_count;
  json << ",\"redis\":" << (snapshot.redis_healthy ? "true" : "false");
  if (!snapshot.instance_id.empty()) {
    json << ",\"instance_id\":\"" << snapshot.instance_id << "\"";
  }
  json << ",\"postgres\":" << (snapshot.postgres_healthy ? "true" : "false");
  json << "}";
  return json.str();
}

}  // namespace poker_engine::network
