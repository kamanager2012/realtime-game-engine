#pragma once

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

#include "poker_engine/network/health_probe.h"

namespace poker_engine::network {

struct ServerMetrics {
  std::atomic<uint64_t> http_requests{0};
  std::atomic<uint64_t> ws_messages{0};
  std::atomic<uint64_t> buy_in_debits{0};
  std::atomic<uint64_t> buy_in_failures{0};
};

inline ServerMetrics& GlobalMetrics() {
  static ServerMetrics metrics;
  return metrics;
}

inline std::string RenderPrometheusMetrics(const ServerMetrics& metrics,
                                           const HealthSnapshot& health) {
  std::ostringstream out;
  out << "# HELP poker_http_requests_total Total HTTP requests handled\n";
  out << "# TYPE poker_http_requests_total counter\n";
  out << "poker_http_requests_total " << metrics.http_requests.load() << "\n";
  out << "# HELP poker_ws_messages_total Total WebSocket messages handled\n";
  out << "# TYPE poker_ws_messages_total counter\n";
  out << "poker_ws_messages_total " << metrics.ws_messages.load() << "\n";
  out << "# HELP poker_buy_in_debits_total Successful buy-in debits\n";
  out << "# TYPE poker_buy_in_debits_total counter\n";
  out << "poker_buy_in_debits_total " << metrics.buy_in_debits.load() << "\n";
  out << "# HELP poker_buy_in_failures_total Failed buy-in debits\n";
  out << "# TYPE poker_buy_in_failures_total counter\n";
  out << "poker_buy_in_failures_total " << metrics.buy_in_failures.load() << "\n";
  out << "# HELP poker_db_healthy Database health status\n";
  out << "# TYPE poker_db_healthy gauge\n";
  out << "poker_db_healthy " << (health.db_healthy ? 1 : 0) << "\n";
  out << "# HELP poker_tables_active Active table count\n";
  out << "# TYPE poker_tables_active gauge\n";
  out << "poker_tables_active " << health.table_count << "\n";
  out << "# HELP poker_redis_healthy Redis connectivity (1=ok)\n";
  out << "# TYPE poker_redis_healthy gauge\n";
  out << "poker_redis_healthy " << (health.redis_healthy ? 1 : 0) << "\n";
  out << "# HELP poker_postgres_healthy PostgreSQL mirror connectivity\n";
  out << "# TYPE poker_postgres_healthy gauge\n";
  out << "poker_postgres_healthy " << (health.postgres_healthy ? 1 : 0) << "\n";
  return out.str();
}

}  // namespace poker_engine::network
