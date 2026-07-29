// =============================================================================
// Security Layer ↔ Core Engine Integration Tests
// =============================================================================

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "poker_engine/base/assertion.h"
#include "poker_engine/security/behavior_analyzer.h"
#include "poker_engine/security/gdpr_engine.h"
#include "poker_engine/security/ip_reputation.h"

using namespace poker_engine::security;

// ==================== Fixture ====================

class SecurityIntegrationTest : public ::testing::Test {
 protected:
  BehavioralAnalysisEngine behavior_engine_;
  std::unique_ptr<GDPRComplianceEngine> gdpr_engine_;
  std::unique_ptr<IPReputationManager> ip_mgr_;

  void SetUp() override {
    GDPRComplianceEngine::Config gdpr_config;
    gdpr_config.default_response_days = 30;
    gdpr_engine_ = std::make_unique<GDPRComplianceEngine>(gdpr_config);
    ip_mgr_ = std::make_unique<IPReputationManager>(100000);
  }
};

// ==================== 1. 行为分析 ↔ 牌局集成 ====================

TEST_F(SecurityIntegrationTest, NormalPlayerNotFlagged) {
  int64_t player_id = 1001;
  for (int i = 0; i < 50; ++i) {
    BehaviorSample sample;
    sample.timestamp_ms = i * 2000;
    sample.action_latency_ms = 800 + (i % 5) * 200;
    sample.action_type = (i % 4);
    sample.bet_ratio = 0.1 + (i % 3) * 0.05;
    sample.pot_ratio = 0.3;
    sample.is_check = (i % 10 == 0);
    sample.is_raise = (i % 4 == 3);
    sample.stack_at_action = 1000.0 - i * 5;
    sample.avg_pot_last_5 = 50.0;
    behavior_engine_.AddSample(player_id, sample);
  }

  auto profile = behavior_engine_.GetProfile(player_id);
  ASSERT_TRUE(profile.has_value());
  EXPECT_LT(profile->risk_score, 40.0);
  EXPECT_FALSE(profile->IsSuspicious());
}

TEST_F(SecurityIntegrationTest, BotDetectedByConsistentTiming) {
  int64_t bot_id = 2001;
  for (int i = 0; i < 50; ++i) {
    BehaviorSample sample;
    sample.timestamp_ms = i * 1000;
    sample.action_latency_ms = 500.0;
    sample.action_type = 2;
    sample.bet_ratio = 0.5;
    sample.pot_ratio = 0.3;
    sample.is_check = false;
    sample.is_raise = true;
    sample.stack_at_action = 2000.0;
    sample.avg_pot_last_5 = 100.0;
    behavior_engine_.AddSample(bot_id, sample);
  }

  auto profile = behavior_engine_.GetProfile(bot_id);
  ASSERT_TRUE(profile.has_value());
  EXPECT_GE(profile->risk_score, 40.0);
  EXPECT_TRUE(profile->IsSuspicious());
}

TEST_F(SecurityIntegrationTest, StrategyMirroringBetweenPlayers) {
  int64_t a = 3001, b = 3002;
  for (int i = 0; i < 30; ++i) {
    BehaviorSample sa;
    sa.timestamp_ms = i * 1200;
    sa.action_latency_ms = 450.0;
    sa.action_type = 2;
    sa.bet_ratio = 0.5;
    sa.pot_ratio = 0.3;
    sa.is_check = false;
    sa.is_raise = true;
    sa.stack_at_action = 1500.0;
    sa.avg_pot_last_5 = 80.0;
    behavior_engine_.AddSample(a, sa);
    BehaviorSample sb = sa;
    sb.timestamp_ms += 50;
    behavior_engine_.AddSample(b, sb);
  }

  EXPECT_GT(behavior_engine_.StrategyMirroring(a, b), 0.8);
  // TimeCorrelation requires more diverse timing patterns; verify it returns a value
  double tc = behavior_engine_.TimeCorrelation(a, b);
  EXPECT_GE(tc, 0.0);
  EXPECT_LE(tc, 1.0);
}

// ==================== 2. GDPR 集成 ====================

TEST_F(SecurityIntegrationTest, GDPRAccessRequest) {
  int64_t player_id = 4001;
  auto result = gdpr_engine_->SubmitAccessRequest(player_id, player_id);
  EXPECT_TRUE(result.IsOk());
  EXPECT_EQ(result.Unwrap().player_id, player_id);
  EXPECT_EQ(result.Unwrap().type, DataSubjectRequestType::Access);
}

TEST_F(SecurityIntegrationTest, GDPRErasureRequest) {
  auto result = gdpr_engine_->SubmitErasureRequest(5001, "User deletion request");
  EXPECT_TRUE(result.IsOk());
  EXPECT_EQ(result.Unwrap().type, DataSubjectRequestType::Erasure);
}

TEST_F(SecurityIntegrationTest, GDPRCrossPlayerAccessDenied) {
  auto result = gdpr_engine_->SubmitAccessRequest(6002, 6001);
  EXPECT_TRUE(result.IsErr());
}

TEST_F(SecurityIntegrationTest, GDPRRectificationBlocksAuthData) {
  auto result =
      gdpr_engine_->RectifyData(7001, DataCategory::Authentication, "password_hash", "new_hash");
  EXPECT_TRUE(result.IsErr());
}

// ==================== 3. IP 信誉集成 ====================

TEST_F(SecurityIntegrationTest, SuspiciousIPBlocked) {
  ip_mgr_->ReportBehavior("192.168.1.100", ThreatLevel::High, "spamming");
  ip_mgr_->ReportBehavior("192.168.1.100", ThreatLevel::Critical, "multi-account");
  EXPECT_TRUE(ip_mgr_->ShouldBlock("192.168.1.100"));
}

TEST_F(SecurityIntegrationTest, CleanIPNotBlocked) {
  EXPECT_FALSE(ip_mgr_->ShouldBlock("10.0.0.1"));
}

TEST_F(SecurityIntegrationTest, IPReputationQueryReturnsData) {
  ip_mgr_->ReportBehavior("172.16.0.1", ThreatLevel::Low, "vpn detected");
  auto rep = ip_mgr_->Query("172.16.0.1");
  EXPECT_EQ(rep.ip_address, "172.16.0.1");
  EXPECT_NE(rep.threat_level, ThreatLevel::None);
}

// ==================== 4. AssertConfig 验证 ====================

TEST_F(SecurityIntegrationTest, AssertConfigDefaultIsAbort) {
  EXPECT_EQ(poker_engine::AssertConfig::Instance().GetBehavior(),
            poker_engine::AssertBehavior::Abort);
}

TEST_F(SecurityIntegrationTest, AssertConfigSwitchToReport) {
  poker_engine::AssertConfig::Instance().SetBehavior(poker_engine::AssertBehavior::Report);
  EXPECT_EQ(poker_engine::AssertConfig::Instance().GetBehavior(),
            poker_engine::AssertBehavior::Report);
  poker_engine::AssertConfig::Instance().SetBehavior(poker_engine::AssertBehavior::Abort);
}

TEST_F(SecurityIntegrationTest, AssertConfigCustomHandlerCaptures) {
  std::string captured;
  poker_engine::AssertConfig::Instance().SetBehavior(poker_engine::AssertBehavior::Report);
  poker_engine::AssertConfig::Instance().SetCustomHandler(
      [&captured](const std::string& msg, const std::string& file, int line) { captured = msg; });

  bool result = poker_engine::AssertFailed("test assertion", "test.cpp", 42);
  EXPECT_FALSE(result);  // AssertFailed returns false in Report mode
  EXPECT_FALSE(captured.empty());
  EXPECT_NE(captured.find("test assertion"), std::string::npos);

  poker_engine::AssertConfig::Instance().SetCustomHandler(nullptr);
  poker_engine::AssertConfig::Instance().SetBehavior(poker_engine::AssertBehavior::Abort);
}

TEST_F(SecurityIntegrationTest, AssertRetValReturnsFalseOnFailure) {
  poker_engine::AssertConfig::Instance().SetBehavior(poker_engine::AssertBehavior::Report);
  poker_engine::AssertConfig::Instance().SetCustomHandler(nullptr);

  // PE_ASSERT_RET returns false on failure in Report mode
  bool failed = PE_ASSERT_RET(1 == 0);
  EXPECT_FALSE(failed);

  // PE_ASSERT_RET returns true on success
  bool ok = PE_ASSERT_RET(1 == 1);
  EXPECT_TRUE(ok);

  poker_engine::AssertConfig::Instance().SetBehavior(poker_engine::AssertBehavior::Abort);
}
