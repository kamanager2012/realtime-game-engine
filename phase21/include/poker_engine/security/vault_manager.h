#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "poker_engine/base/result.h"

namespace poker_engine::security {

// ==================== 密钥提供者接口 ====================

enum class SecretBackend {
  Vault,      // HashiCorp Vault
  K8sSecret,  // Kubernetes Secrets
  EnvVar,     // 环境变量 (开发用)
  File,       // 加密文件 (本地开发)
};

// 密钥元数据
struct SecretMetadata {
  std::string key;
  std::string version;
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point expires_at;
  std::string lease_id;  // Vault lease
  bool renewable = false;
};

// ==================== Vault 客户端接口 ====================

class VaultClient {
 public:
  virtual ~VaultClient() = default;

  // 连接
  virtual base::Result<void> Connect(const std::string& address, const std::string& token) = 0;

  // 读取密钥
  virtual base::Result<std::string> ReadSecret(const std::string& path,
                                               const std::string& key = "") = 0;

  // 写入密钥
  virtual base::Result<void> WriteSecret(const std::string& path, const std::string& key,
                                         const std::string& value) = 0;

  // 续租
  virtual base::Result<void> RenewLease(const std::string& lease_id) = 0;

  // 动态数据库凭证
  virtual base::Result<std::pair<std::string, std::string>> GetDatabaseCredential(
      const std::string& role) = 0;
};

// ==================== K8s Secret 客户端 ====================

class K8sSecretClient {
 public:
  K8sSecretClient(const std::string& namespace_name = "poker-engine");

  base::Result<std::string> GetSecret(const std::string& name, const std::string& key);

  base::Result<void> CreateSecret(const std::string& name,
                                  const std::unordered_map<std::string, std::string>& data);

  base::Result<void> DeleteSecret(const std::string& name);

  // 使用 ServiceAccount token 认证
  base::Result<void> Authenticate();

 private:
  std::string namespace_;
  std::string api_token_;
  std::string api_endpoint_ = "https://kubernetes.default.svc";

  std::string ReadServiceAccountToken() const;
  std::string Base64Decode(const std::string& encoded) const;
  std::string Base64Encode(const std::string& data) const;
};

// ==================== 密钥管理服务 ====================
// 统一接口，支持多种后端

struct VaultManagerConfig {
  SecretBackend backend = SecretBackend::EnvVar;
  std::string vault_address;
  std::string vault_token;
  std::string vault_mount = "secret";
  std::string k8s_namespace = "poker-engine";
  std::string fallback_prefix = "POKER_ENGINE_";
  bool auto_renew_leases = true;
  std::chrono::seconds lease_renewal_interval = std::chrono::minutes(5);
};

class VaultManager {
 public:
  using Config = VaultManagerConfig;

  explicit VaultManager(const Config& config = Config());
  ~VaultManager();

  // 初始化连接
  base::Result<void> Initialize();

  // 获取密钥
  base::Result<std::string> GetSecret(const std::string& key);

  // 缓存的密钥获取（带 TTL）
  base::Result<std::string> GetCachedSecret(
      const std::string& key, std::chrono::seconds cache_ttl = std::chrono::seconds(300));

  // 获取数据库凭证（动态凭证）
  base::Result<std::pair<std::string, std::string>> GetDatabaseCredential(
      const std::string& role = "poker-app");

  // 获取 JWT 签名密钥
  base::Result<std::string> GetJWTSigningKey();

  // 获取 TLS 证书
  base::Result<std::pair<std::string, std::string>>  // cert, key
  GetTLSCertificate(const std::string& domain);

  // 密钥轮换
  base::Result<void> RotateKey(const std::string& key, const std::string& new_value);

  // 续租
  base::Result<void> RenewLease(const std::string& lease_id);

  // 定期续租线程
  void StartLeaseRenewal();
  void StopLeaseRenewal();

  // 诊断
  bool IsConnected() const { return connected_; }
  SecretBackend ActiveBackend() const { return config_.backend; }

 private:
  Config config_;
  std::unique_ptr<VaultClient> vault_client_;
  std::unique_ptr<K8sSecretClient> k8s_client_;

  // 本地缓存
  struct CachedSecret {
    std::string value;
    std::chrono::steady_clock::time_point cached_at;
    std::chrono::seconds ttl;
    std::string lease_id;  // Vault lease ID
  };
  std::unordered_map<std::string, CachedSecret> secret_cache_;
  mutable std::mutex cache_mutex_;

  bool connected_ = false;
  std::thread renewal_thread_;
  std::atomic<bool> renewal_running_{false};

  // 后端选择
  base::Result<std::string> GetFromVault(const std::string& key);
  base::Result<std::string> GetFromK8s(const std::string& key);
  base::Result<std::string> GetFromEnv(const std::string& key);

  void LeaseRenewalLoop();
};

}  // namespace poker_engine::security
