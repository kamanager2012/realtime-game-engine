#pragma once

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <array>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace poker_engine::security {

// ==================== 加密工具集合 ====================
// 提供对称加密、HMAC、密钥派生等安全原语

class CryptoUtils {
 public:
  // 对称加密算法
  static constexpr const char* kCipherName = "aes-256-gcm";
  static constexpr size_t kKeySize = 32;  // 256 bits
  static constexpr size_t kIVSize = 12;   // 96 bits (GCM 推荐)
  static constexpr size_t kTagSize = 16;  // 128 bits

  // ========== 随机数生成 ==========

  // 加密安全随机数
  static std::vector<uint8_t> RandomBytes(size_t count) {
    std::vector<uint8_t> result(count);
    RAND_bytes(result.data(), count);
    return result;
  }

  // 生成随机字符串（十六进制）
  static std::string RandomHex(size_t bytes = 32) {
    auto random = RandomBytes(bytes);
    std::ostringstream oss;
    for (auto b : random) {
      oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
  }

  // ========== AES-256-GCM 加密 ==========

  struct EncryptedData {
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> iv;   // 12 bytes
    std::vector<uint8_t> tag;  // 16 bytes
    std::string b64() const;   // Base64 编码完整数据
  };

  // 对称加密
  static EncryptedData Encrypt(const std::vector<uint8_t>& plaintext,
                               const std::vector<uint8_t>& key);

  // 对称解密
  static std::vector<uint8_t> Decrypt(const EncryptedData& encrypted,
                                      const std::vector<uint8_t>& key);

  // ========== HMAC ==========

  // HMAC-SHA256
  static std::vector<uint8_t> HMAC256(const std::vector<uint8_t>& key,
                                      const std::vector<uint8_t>& message);

  // HMAC 验证
  static bool VerifyHMAC(const std::vector<uint8_t>& key, const std::vector<uint8_t>& message,
                         const std::vector<uint8_t>& expected_mac);

  // ========== 密钥派生 (PBKDF2) ==========

  struct DerivedKey {
    std::vector<uint8_t> key;
    std::vector<uint8_t> salt;
    int iterations;
  };

  // PBKDF2 密钥派生
  static DerivedKey DeriveKey(const std::string& password, size_t key_length = 32,
                              int iterations = 100000);

  // 验证密码
  static bool VerifyPassword(const std::string& password, const DerivedKey& dk);

  // ========== 哈希 ==========

  static std::vector<uint8_t> SHA256(const std::vector<uint8_t>& data);

  static std::string SHA256Hex(const std::string& data);

  // ========== Base64 ==========

  static std::string Base64Encode(const std::vector<uint8_t>& data);
  static std::vector<uint8_t> Base64Decode(const std::string& encoded);

  // ========== 令牌生成 ==========

  // 生成安全的会话令牌
  static std::string GenerateToken() {
    return RandomHex(32);  // 64 字符十六进制 = 256 bits 熵
  }

  // 令牌哈希存储
  static std::string HashToken(const std::string& token) { return SHA256Hex(token); }

 private:
  // 初始化 OpenSSL 算法
  static void EnsureEvpInit();
};

}  // namespace poker_engine::security
