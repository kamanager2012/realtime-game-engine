#include <gtest/gtest.h>

#include "poker_engine/network/health_probe.h"

using namespace poker_engine::network;

TEST(HealthProbeTest, BuildsProductionPayload) {
  HealthSnapshot s;
  s.production_mode = true;
  s.db_healthy = true;
  s.table_count = 3;
  const std::string json = BuildHealthJson(s);
  EXPECT_NE(json.find("\"status\":\"ok\""), std::string::npos);
  EXPECT_NE(json.find("\"mode\":\"production\""), std::string::npos);
  EXPECT_NE(json.find("\"db\":true"), std::string::npos);
  EXPECT_NE(json.find("\"tables\":3"), std::string::npos);
}

TEST(HealthProbeTest, DevelopmentWithoutDb) {
  HealthSnapshot s;
  const std::string json = BuildHealthJson(s);
  EXPECT_NE(json.find("\"mode\":\"development\""), std::string::npos);
  EXPECT_NE(json.find("\"db\":false"), std::string::npos);
  EXPECT_NE(json.find("\"tables\":0"), std::string::npos);
}


TEST(HealthProbeTest, IncludesRedisAndInstanceFields) {
  HealthSnapshot s;
  s.redis_healthy = true;
  s.instance_id = "node-1";
  const std::string json = BuildHealthJson(s);
  EXPECT_NE(json.find("\"redis\":true"), std::string::npos);
  EXPECT_NE(json.find("\"instance_id\":\"node-1\""), std::string::npos);
}
