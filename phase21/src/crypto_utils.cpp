#include "poker_engine/security/crypto_utils.h"

#include <openssl/buffer.h>

#include <iomanip>
#include <mutex>
#include <sstream>

namespace poker_engine::security {

// ==================== AES-256-GCM ====================

CryptoUtils::EncryptedData CryptoUtils::Encrypt(const std::vector<uint8_t>& plaintext,
                                                const std::vector<uint8_t>& key) {
  EnsureEvpInit();

  EncryptedData result;
  result.iv = RandomBytes(kIVSize);

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

  if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("EncryptInit failed");
  }

  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIVSize, nullptr) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("Set IV length failed");
  }

  if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), result.iv.data()) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("EncryptInit key/iv failed");
  }

  result.ciphertext.resize(plaintext.size() + 16);
  int len = 0, ciphertext_len = 0;

  if (EVP_EncryptUpdate(ctx, result.ciphertext.data(), &len, plaintext.data(), plaintext.size()) !=
      1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("EncryptUpdate failed");
  }
  ciphertext_len = len;

  if (EVP_EncryptFinal_ex(ctx, result.ciphertext.data() + len, &len) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("EncryptFinal failed");
  }
  ciphertext_len += len;

  result.tag.resize(kTagSize);
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagSize, result.tag.data()) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("Get GCM tag failed");
  }

  result.ciphertext.resize(ciphertext_len);
  EVP_CIPHER_CTX_free(ctx);

  return result;
}

std::vector<uint8_t> CryptoUtils::Decrypt(const EncryptedData& encrypted,
                                          const std::vector<uint8_t>& key) {
  EnsureEvpInit();

  if (encrypted.iv.size() != kIVSize || encrypted.tag.size() != kTagSize) {
    throw std::invalid_argument("Invalid encrypted data format");
  }

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("DecryptInit failed");
  }

  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIVSize, nullptr) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("Set IV length failed");
  }

  if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), encrypted.iv.data()) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("DecryptInit key/iv failed");
  }

  std::vector<uint8_t> plaintext(encrypted.ciphertext.size() + 16);
  int len = 0, plaintext_len = 0;

  if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, encrypted.ciphertext.data(),
                        encrypted.ciphertext.size()) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("DecryptUpdate failed");
  }
  plaintext_len = len;

  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagSize,
                          const_cast<uint8_t*>(encrypted.tag.data())) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("Set GCM tag failed");
  }

  if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("DecryptFinal failed - authentication failed");
  }
  plaintext_len += len;

  plaintext.resize(plaintext_len);
  EVP_CIPHER_CTX_free(ctx);

  return plaintext;
}

// ==================== HMAC-SHA256 ====================

std::vector<uint8_t> CryptoUtils::HMAC256(const std::vector<uint8_t>& key,
                                          const std::vector<uint8_t>& message) {
  std::vector<uint8_t> result(EVP_MAX_MD_SIZE);
  unsigned int result_len = 0;

  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), message.data(), message.size(),
       result.data(), &result_len);

  result.resize(result_len);
  return result;
}

bool CryptoUtils::VerifyHMAC(const std::vector<uint8_t>& key, const std::vector<uint8_t>& message,
                             const std::vector<uint8_t>& expected_mac) {
  auto computed = HMAC256(key, message);

  if (computed.size() != expected_mac.size()) return false;

  // 恒定时间比较防止时序攻击
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < computed.size(); ++i) {
    diff |= computed[i] ^ expected_mac[i];
  }
  return diff == 0;
}

// ==================== PBKDF2 密钥派生 ====================

CryptoUtils::DerivedKey CryptoUtils::DeriveKey(const std::string& password, size_t key_length,
                                               int iterations) {
  DerivedKey dk;
  dk.key.resize(key_length);
  dk.salt = RandomBytes(16);
  dk.iterations = iterations;

  PKCS5_PBKDF2_HMAC(password.c_str(), password.size(), dk.salt.data(), dk.salt.size(), iterations,
                    EVP_sha256(), key_length, dk.key.data());

  return dk;
}

bool CryptoUtils::VerifyPassword(const std::string& password, const DerivedKey& dk) {
  std::vector<uint8_t> test_key(dk.key.size());

  PKCS5_PBKDF2_HMAC(password.c_str(), password.size(), dk.salt.data(), dk.salt.size(),
                    dk.iterations, EVP_sha256(), dk.key.size(), test_key.data());

  // 恒定时间比较
  if (test_key.size() != dk.key.size()) return false;
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < test_key.size(); ++i) {
    diff |= test_key[i] ^ dk.key[i];
  }
  return diff == 0;
}

// ==================== 哈希 ====================

std::vector<uint8_t> CryptoUtils::SHA256(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> hash(32);
  unsigned int hash_len = 0;

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");

  if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(ctx, data.data(), data.size()) != 1 ||
      EVP_DigestFinal_ex(ctx, hash.data(), &hash_len) != 1) {
    EVP_MD_CTX_free(ctx);
    throw std::runtime_error("SHA256 digest failed");
  }

  EVP_MD_CTX_free(ctx);
  hash.resize(hash_len);
  return hash;
}

std::string CryptoUtils::SHA256Hex(const std::string& data) {
  auto hash = SHA256(std::vector<uint8_t>(data.begin(), data.end()));
  std::ostringstream oss;
  for (auto b : hash) {
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
  }
  return oss.str();
}

// ==================== Base64 ====================

std::string CryptoUtils::Base64Encode(const std::vector<uint8_t>& data) {
  BIO* bio = BIO_new(BIO_s_mem());
  BIO* b64 = BIO_new(BIO_f_base64());
  bio = BIO_push(b64, bio);

  BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(bio, data.data(), data.size());
  BIO_flush(bio);

  BUF_MEM* mem;
  BIO_get_mem_ptr(bio, &mem);
  std::string result(mem->data, mem->length);

  BIO_free_all(bio);
  return result;
}

std::vector<uint8_t> CryptoUtils::Base64Decode(const std::string& encoded) {
  BIO* bio = BIO_new_mem_buf(encoded.c_str(), encoded.size());
  BIO* b64 = BIO_new(BIO_f_base64());
  bio = BIO_push(b64, bio);

  BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

  std::vector<uint8_t> result(encoded.size());
  int len = BIO_read(bio, result.data(), encoded.size());
  result.resize(len > 0 ? len : 0);

  BIO_free_all(bio);
  return result;
}

// ==================== EncryptedData B64 ====================

std::string CryptoUtils::EncryptedData::b64() const {
  std::vector<uint8_t> combined;
  combined.reserve(iv.size() + tag.size() + ciphertext.size());
  combined.insert(combined.end(), iv.begin(), iv.end());
  combined.insert(combined.end(), tag.begin(), tag.end());
  combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());
  return Base64Encode(combined);
}

// ==================== 工具函数实现 ====================

void CryptoUtils::EnsureEvpInit() {
  // OpenSSL 3.x auto-initializes; no manual init needed
}

}  // namespace poker_engine::security
