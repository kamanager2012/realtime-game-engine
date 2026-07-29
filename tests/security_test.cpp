#include <gtest/gtest.h>

#include <iostream>

#include "poker_engine/base/audit_logger.h"
#include "poker_engine/security/behavior_analyzer.h"
#include "poker_engine/security/crypto_utils.h"
#include "poker_engine/security/gdpr_engine.h"
#include "poker_engine/security/ip_reputation.h"
#include "poker_engine/security/mtls_service.h"
#include "poker_engine/security/security_policy_engine.h"
#include "poker_engine/security/vault_manager.h"

using namespace poker_engine::security;

// ==================== AES-256-GCM 加密测试 ====================

class SecurityTest : public ::testing::Test {
 protected:
  std::vector<uint8_t> key_;

  void SetUp() override { key_ = CryptoUtils::RandomBytes(32); }
};

TEST_F(SecurityTest, AES256GCM_EncryptDecrypt) {
  std::string plaintext = "Secret poker hand data - Ace of Spades!";
  std::vector<uint8_t> data(plaintext.begin(), plaintext.end());

  auto encrypted = CryptoUtils::Encrypt(data, key_);

  EXPECT_FALSE(encrypted.ciphertext.empty());
  EXPECT_EQ(encrypted.iv.size(), 12u);
  EXPECT_EQ(encrypted.tag.size(), 16u);

  // 密文应与明文不同
  EXPECT_NE(std::string(encrypted.ciphertext.begin(), encrypted.ciphertext.end()), plaintext);

  // 解密
  auto decrypted = CryptoUtils::Decrypt(encrypted, key_);
  EXPECT_EQ(std::string(decrypted.begin(), decrypted.end()), plaintext);

  // 错误密钥应失败
  std::vector<uint8_t> wrong_key(32, 0xFF);
  EXPECT_THROW(CryptoUtils::Decrypt(encrypted, wrong_key), std::runtime_error);
}

TEST_F(SecurityTest, HMAC256Verification) {
  std::vector<uint8_t> key = {'s', 'e', 'c', 'r', 'e', 't', '_', 'k', 'e', 'y'};
  std::vector<uint8_t> message = {'h', 'e', 'l', 'l', 'o'};

  auto mac = CryptoUtils::HMAC256(key, message);
  EXPECT_EQ(mac.size(), 32u);

  EXPECT_TRUE(CryptoUtils::VerifyHMAC(key, message, mac));

  // 篡改消息应失败
  std::vector<uint8_t> tampered = {'h', 'e', 'l', 'l', 'x'};
  EXPECT_FALSE(CryptoUtils::VerifyHMAC(key, tampered, mac));
}

TEST_F(SecurityTest, PBKDF2KeyDerivation) {
  std::string password = "my_secure_password_123";

  auto dk = CryptoUtils::DeriveKey(password, 32, 100000);
  EXPECT_EQ(dk.key.size(), 32u);
  EXPECT_EQ(dk.salt.size(), 16u);

  EXPECT_TRUE(CryptoUtils::VerifyPassword(password, dk));
  EXPECT_FALSE(CryptoUtils::VerifyPassword("wrong_password", dk));
}

TEST_F(SecurityTest, TokenGeneration) {
  std::string token1 = CryptoUtils::GenerateToken();
  std::string token2 = CryptoUtils::GenerateToken();

  EXPECT_EQ(token1.size(), 64u);
  EXPECT_NE(token1, token2);
  EXPECT_NE(CryptoUtils::HashToken(token1), CryptoUtils::HashToken(token2));
}

TEST_F(SecurityTest, Base64Roundtrip) {
  std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
  std::string encoded = CryptoUtils::Base64Encode(data);
  auto decoded = CryptoUtils::Base64Decode(encoded);
  EXPECT_EQ(decoded, data);
}

// ==================== IP 信誉测试 ====================

TEST_F(SecurityTest, IPReputationQuery) {
  IPReputationManager mgr;

  auto rep = mgr.Query("192.168.1.1");
  EXPECT_GE(rep.score, 60.0);

  rep = mgr.Query("1.2.3.4.onion");
  EXPECT_LT(rep.score, 20.0);
  EXPECT_TRUE(rep.is_tor_exit);
}

TEST_F(SecurityTest, IPBehaviorReporting) {
  IPReputationManager mgr;

  auto rep = mgr.Query("10.0.0.1");
  double initial = rep.score;

  mgr.ReportBehavior("10.0.0.1", ThreatLevel::High, "Brute force");

  auto rep2 = mgr.Query("10.0.0.1");
  EXPECT_LT(rep2.score, initial);
}

// ==================== 行为分析测试 ====================

TEST_F(SecurityTest, BehavioralAnalysisConsistency) {
  BehavioralAnalysisEngine engine;

  for (int i = 0; i < 100; ++i) {
    BehaviorSample human, bot;
    human.action_latency_ms = 1000 + (rand() % 5000);
    human.action_type = i % 4;
    human.bet_ratio = 0.5 + (rand() % 100) / 100.0;

    bot.action_latency_ms = 2000 + (rand() % 50);
    bot.action_type = i % 4;
    bot.bet_ratio = 0.7;

    engine.AddSample(1, human);
    engine.AddSample(2, bot);
  }

  auto p1 = engine.GetProfile(1);
  auto p2 = engine.GetProfile(2);

  ASSERT_TRUE(p1.has_value());
  ASSERT_TRUE(p2.has_value());

  EXPECT_LT(p2->response_time_variance, p1->response_time_variance);
}

TEST_F(SecurityTest, StrategyMirroringDetection) {
  BehavioralAnalysisEngine engine;

  for (int i = 0; i < 50; ++i) {
    BehaviorSample s1, s2;
    s1.action_type = s2.action_type = (i % 4);
    s1.bet_ratio = s2.bet_ratio = 0.5;
    s1.action_latency_ms = 2000 + i;
    s2.action_latency_ms = 2000 + i;

    engine.AddSample(100, s1);
    engine.AddSample(101, s2);
  }

  EXPECT_GT(engine.StrategyMirroring(100, 101), 0.9);
  EXPECT_GT(engine.TimeCorrelation(100, 101), 0.5);
}

// ==================== 安全策略引擎测试 ====================

TEST_F(SecurityTest, RiskScoring) {
  RiskAssessment a;
  a.ip_score = 80;
  a.device_score = 70;
  a.behavior_score = 90;
  a.ml_score = 85;
  a.historical_score = 75;
  a.Normalize();

  EXPECT_NEAR(a.overall_score, 83.5, 0.1);
}

TEST_F(SecurityTest, AutoBlockDecision) {
  SecurityPolicyEngine engine;

  RiskAssessment low;
  low.player_id = "1";
  low.ip_score = 20;
  low.device_score = 20;
  low.behavior_score = 20;
  low.ml_score = 20;
  low.historical_score = 20;
  low.Normalize();
  EXPECT_EQ(engine.MakeDecision(low).action, SecurityPolicyEngine::ActionType::Allow);

  RiskAssessment high;
  high.player_id = "2";
  high.ip_score = 95;
  high.device_score = 95;
  high.behavior_score = 95;
  high.ml_score = 95;
  high.historical_score = 95;
  high.critical_flags = {"CONFIRMED_BOT"};
  high.Normalize();
  EXPECT_EQ(engine.MakeDecision(high).action, SecurityPolicyEngine::ActionType::Ban);
}
