#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "poker_engine/security/gdpr_engine.h"

using namespace poker_engine::security;

class GDPRTest : public ::testing::Test {
 protected:
  GDPRComplianceEngine engine{GDPRConfig{30, 365, true, true, "dpo@test.com"}};
};

TEST_F(GDPRTest, ConsentManagement) {
  engine.RecordConsent(1001, DataCategory::PersonalIdentity, true, "test");
  EXPECT_TRUE(engine.HasConsent(1001, DataCategory::PersonalIdentity));

  engine.WithdrawConsent(1001);
  EXPECT_FALSE(engine.HasConsent(1001, DataCategory::PersonalIdentity));
}

TEST_F(GDPRTest, AccessRequest) {
  auto result = engine.SubmitAccessRequest(1001, 1001);
  ASSERT_TRUE(result.IsOk());
  EXPECT_EQ(result.Unwrap().type, DataSubjectRequestType::Access);
  EXPECT_EQ(result.Unwrap().status, RequestStatus::Pending);
}

TEST_F(GDPRTest, UnauthorizedAccessRejected) {
  auto result = engine.SubmitAccessRequest(1001, 9999);
  EXPECT_FALSE(result.IsOk());
}

TEST_F(GDPRTest, ErasureRequest) {
  auto result = engine.SubmitErasureRequest(1001, "User requested");
  ASSERT_TRUE(result.IsOk());
  EXPECT_EQ(result.Unwrap().type, DataSubjectRequestType::Erasure);
}

TEST_F(GDPRTest, DataPortability) {
  engine.RecordConsent(1001, DataCategory::PersonalIdentity, true, "export");
  auto result = engine.ExportDataPortability(1001);
  EXPECT_TRUE(result.IsOk());

  auto json = nlohmann::json::parse(result.Unwrap());
  EXPECT_TRUE(json["gdpr_export"].get<bool>());
}

TEST_F(GDPRTest, Rectification) {
  EXPECT_TRUE(engine.RectifyData(1001, DataCategory::PersonalIdentity, "name", "New Name").IsOk());
  EXPECT_FALSE(engine.RectifyData(1001, DataCategory::Authentication, "hash", "new").IsOk());
}

TEST_F(GDPRTest, ProcessingRestriction) {
  EXPECT_FALSE(engine.IsPlayerDataRestricted(1001));
  engine.RestrictProcessing(1001, DataCategory::Behavioral);
  EXPECT_TRUE(engine.IsPlayerDataRestricted(1001));
}

TEST_F(GDPRTest, BreachReport) {
  EXPECT_TRUE(engine.ReportBreach("Test breach", {1001}, "low").IsOk());
  EXPECT_TRUE(engine.ReportBreach("Serious breach", {1001}, "high").IsOk());
}
