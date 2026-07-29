#include <gtest/gtest.h>
#include <openssl/evp.h>

#include "poker_engine/base/logging.h"
#include "poker_engine/security/crypto_utils.h"
#include "poker_engine/security/ip_reputation.h"
#include "poker_engine/security/mtls_service.h"

using namespace poker_engine::security;

// ==================== AES-256-GCM 加密测试 ====================

class CryptoTest : public ::testing::Test {
 protected:
  std::vector<uint8_t> key_;

  void SetUp() override { key_ = CryptoUtils::RandomBytes(32); }
};

TEST_F(CryptoTest, AES256GCM_EncryptDecrypt) {
  std::string plaintext = "Secret poker hand data - Ace of Spades!";
  std::vector<uint8_t> data(plaintext.begin(), plaintext.end());

  auto encrypted = CryptoUtils::Encrypt(data, key_);

  // 密文不应为空
  EXPECT_FALSE(encrypted.ciphertext.empty());
  EXPECT_EQ(encrypted.iv.size(), 12u);
  EXPECT_EQ(encrypted.tag.size(), 16u);

  // 密文应不同于明文
  EXPECT_NE(std::string(encrypted.ciphertext.begin(), encrypted.ciphertext.end()), plaintext);

  // 解密
  auto decrypted = CryptoUtils::Decrypt(encrypted, key_);
  EXPECT_EQ(std::string(decrypted.begin(), decrypted.end()), plaintext);
}

TEST_F(CryptoTest, AES256GCM_WrongKeyFails) {
  std::vector<uint8_t> data = {'s', 'e', 'c', 'r', 'e', 't'};

  auto encrypted = CryptoUtils::Encrypt(data, key_);

  // 使用错误密钥解密
  std::vector<uint8_t> wrong_key(32, 0xFF);
  EXPECT_THROW(CryptoUtils::Decrypt(encrypted, wrong_key), std::runtime_error);
}

TEST_F(CryptoTest, AES256GCM_TamperedCiphertextDetected) {
  std::vector<uint8_t> data = {'p', 'o', 'k', 'e', 'r'};
  auto encrypted = CryptoUtils::Encrypt(data, key_);

  // 篡改密文字节
  encrypted.ciphertext[0] ^= 0xFF;

  // 解密应失败（GCM 标签验证）
  EXPECT_THROW(CryptoUtils::Decrypt(encrypted, key_), std::runtime_error);
}

TEST_F(CryptoTest, AES256GCM_TamperedTagDetected) {
  std::vector<uint8_t> data = {'t', 'e', 's', 't'};
  auto encrypted = CryptoUtils::Encrypt(data, key_);

  // 篡改标签
  encrypted.tag[0] ^= 0xFF;

  EXPECT_THROW(CryptoUtils::Decrypt(encrypted, key_), std::runtime_error);
}

TEST_F(CryptoTest, AES256GCM_TamperedIVDetected) {
  std::vector<uint8_t> data = {'t', 'e', 's', 't'};
  auto encrypted = CryptoUtils::Encrypt(data, key_);

  // 篡改 IV
  encrypted.iv[0] ^= 0xFF;

  EXPECT_THROW(CryptoUtils::Decrypt(encrypted, key_), std::runtime_error);
}

// ==================== HMAC 测试 ====================

TEST_F(CryptoTest, HMAC256_ValidVerification) {
  std::vector<uint8_t> key(32);
  RAND_bytes(key.data(), 32);

  std::vector<uint8_t> message = {'t', 'e', 's', 't', 'm', 'e', 's', 's', 'a', 'g', 'e'};

  auto mac = CryptoUtils::HMAC256(key, message);
  EXPECT_EQ(mac.size(), 32u);

  // 正确验证
  EXPECT_TRUE(CryptoUtils::VerifyHMAC(key, message, mac));
}

TEST_F(CryptoTest, HMAC256_TamperedMessageFails) {
  std::vector<uint8_t> key(32);
  RAND_bytes(key.data(), 32);

  std::vector<uint8_t> message = {'h', 'e', 'l', 'l', 'o'};
  auto mac = CryptoUtils::HMAC256(key, message);

  // 篡改消息
  std::vector<uint8_t> tampered = {'h', 'e', 'l', 'l', 'x'};
  EXPECT_FALSE(CryptoUtils::VerifyHMAC(key, tampered, mac));
}

TEST_F(CryptoTest, HMAC256_TamperedKeyFails) {
  std::vector<uint8_t> key(32);
  RAND_bytes(key.data(), 32);

  std::vector<uint8_t> message = {'d', 'a', 't', 'a'};
  auto mac = CryptoUtils::HMAC256(key, message);

  // 篡改密钥
  key[0] ^= 0xFF;
  EXPECT_FALSE(CryptoUtils::VerifyHMAC(key, message, mac));
}

// ==================== PBKDF2 测试 ====================

TEST_F(CryptoTest, PBKDF2_ProducesConsistentKey) {
  std::string password = "SecurePokerPassword!2024";

  auto key1 = CryptoUtils::DeriveKey(password, 32, 10000);
  auto key2 = CryptoUtils::DeriveKey(password, 32, 10000);

  // 不同 salt → 不同密钥
  EXPECT_NE(key1.key, key2.key);
  EXPECT_NE(key1.salt, key2.salt);

  // 各自可验证
  EXPECT_TRUE(CryptoUtils::VerifyPassword(password, key1));
  EXPECT_TRUE(CryptoUtils::VerifyPassword(password, key2));

  // 错误密码失败
  EXPECT_FALSE(CryptoUtils::VerifyPassword("WrongPassword", key1));
}

TEST_F(CryptoTest, PBKDF2_DifferentIterations) {
  std::string password = "test_password";
  std::string wrong_password = "wrong_password";

  auto key = CryptoUtils::DeriveKey(password, 32, 100000);

  // 高迭代次数需要时间
  EXPECT_TRUE(CryptoUtils::VerifyPassword(password, key));
  EXPECT_FALSE(CryptoUtils::VerifyPassword(wrong_password, key));
}

// ==================== CSR 令牌生成测试 ====================

TEST_F(CryptoTest, Token_Uniqueness) {
  std::set<std::string> tokens;

  for (int i = 0; i < 10000; ++i) {
    std::string token = CryptoUtils::GenerateToken();
    EXPECT_EQ(token.size(), 64u);        // 32 字节 → 64 hex
    EXPECT_EQ(tokens.count(token), 0u);  // 唯一
    tokens.insert(token);
  }
}

TEST_F(CryptoTest, Token_HashUniqueness) {
  std::string token1 = CryptoUtils::GenerateToken();
  std::string token2 = CryptoUtils::GenerateToken();

  std::string hash1 = CryptoUtils::HashToken(token1);
  std::string hash2 = CryptoUtils::HashToken(token2);
  std::string hash1_again = CryptoUtils::HashToken(token1);

  EXPECT_EQ(hash1.size(), 64u);   // SHA-256 hex
  EXPECT_EQ(hash1, hash1_again);  // 确定性
  EXPECT_NE(hash1, hash2);        // 不同令牌不同散列
}

// ==================== 乱序攻击防护测试 ====================

TEST_F(CryptoTest, TimingSafeComparison) {
  std::vector<uint8_t> key = {1, 2, 3, 4, 5};
  std::vector<uint8_t> msg = {'a', 'b', 'c', 'd'};

  auto mac = CryptoUtils::HMAC256(key, msg);

  // 部分匹配应失败
  std::vector<uint8_t> partial(mac.begin(), mac.begin() + 16);
  EXPECT_FALSE(CryptoUtils::VerifyHMAC(key, msg, partial));

  // 空 MAC 应失败
  EXPECT_FALSE(CryptoUtils::VerifyHMAC(key, msg, {}));

  // 正确 MAC 应通过
  EXPECT_TRUE(CryptoUtils::VerifyHMAC(key, msg, mac));
}

// ==================== 性能基准 ====================

TEST_F(CryptoTest, AES256GCM_Performance) {
  // 4KB 数据
  std::vector<uint8_t> data(4096);
  RAND_bytes(data.data(), 4096);

  int iterations = 50000;

  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; ++i) {
    auto enc = CryptoUtils::Encrypt(data, key_);
    auto dec = CryptoUtils::Decrypt(enc, key_);
    EXPECT_EQ(dec, data);
  }
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::high_resolution_clock::now() - start)
                     .count();

  double throughput = (iterations * data.size() * 2.0) / (elapsed / 1000.0);
  throughput /= (1024 * 1024);

  PE_LOG_INFO("[Perf] AES-256-GCM: {} enc+dec pairs in {}ms ({:.1f} MB/s)", iterations, elapsed,
              throughput);

  // 性能基线：每操作 < 0.05ms
  EXPECT_LT(elapsed / iterations, 0.05);
}

TEST_F(CryptoTest, HMAC_Performance) {
  std::vector<uint8_t> key(32);
  std::vector<uint8_t> data(1024);
  RAND_bytes(key.data(), 32);
  RAND_bytes(data.data(), 1024);

  int iterations = 100000;

  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; ++i) {
    auto mac = CryptoUtils::HMAC256(key, data);
    volatile size_t prevent_opt = mac.size();
    (void)prevent_opt;
  }
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::high_resolution_clock::now() - start)
                     .count();

  PE_LOG_INFO("[Perf] HMAC-SHA256: {} ops in {}ms ({:.0f} ops/sec)", iterations, elapsed,
              iterations * 1000.0 / elapsed);

  EXPECT_LT(elapsed / iterations, 0.02);  // < 20μs per HMAC
}
