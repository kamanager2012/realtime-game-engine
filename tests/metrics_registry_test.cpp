#include <gtest/gtest.h>

#include "poker_engine/network/health_probe.h"
#include "poker_engine/network/metrics_registry.h"

using namespace poker_engine::network;

TEST(MetricsRegistryTest, RendersPrometheusCounters) {
  ServerMetrics metrics;
  metrics.http_requests.store(7);
  metrics.buy_in_debits.store(3);
  metrics.buy_in_failures.store(1);

  HealthSnapshot health;
  health.db_healthy = true;
  health.table_count = 2;

  const std::string body = RenderPrometheusMetrics(metrics, health);
  EXPECT_NE(body.find("poker_http_requests_total 7"), std::string::npos);
  EXPECT_NE(body.find("poker_buy_in_debits_total 3"), std::string::npos);
  EXPECT_NE(body.find("poker_buy_in_failures_total 1"), std::string::npos);
  EXPECT_NE(body.find("poker_db_healthy 1"), std::string::npos);
  EXPECT_NE(body.find("poker_tables_active 2"), std::string::npos);
}
