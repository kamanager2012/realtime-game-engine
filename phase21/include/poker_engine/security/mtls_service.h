#pragma once

#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "poker_engine/base/result.h"

namespace poker_engine::security {

// ==================== 证书信息 ====================

struct CertificateInfo {
  std::string subject_cn;  // Common Name
  std::string issuer_cn;   // Issuer CN
  std::string serial_number;
  std::string fingerprint_sha256;
  std::chrono::system_clock::time_point not_before;
  std::chrono::system_clock::time_point not_after;
  bool is_ca = false;
  std::vector<std::string> san_dns;  // Subject Alternative Names

  bool IsValid() const {
    auto now = std::chrono::system_clock::now();
    return now >= not_before && now <= not_after;
  }

  int DaysUntilExpiry() const {
    auto now = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::hours>(not_after - now).count();
    return static_cast<int>(diff / 24);
  }
};

// ==================== 客户端证书验证结果 ====================

enum class CertVerifyResult {
  OK = 0,
  Expired = 1,
  Revoked = 2,
  InvalidSignature = 3,
  UnknownCA = 4,
  CNMismatch = 5,
  Blacklisted = 6,
};

struct ClientCertContext {
  int64_t player_id;
  std::string session_id;
  std::string cert_fingerprint;
  std::string client_ip;
  CertificateInfo cert_info;
  CertVerifyResult verify_result = CertVerifyResult::OK;
  std::chrono::steady_clock::time_point connected_at;

  // 设备指纹（用于绑定）
  std::string device_fingerprint;
};

// ==================== CRL 管理器 ====================

class CRLManager {
 public:
  CRLManager();

  // 从 URL 加载 CRL
  base::Result<void> LoadCRL(const std::string& crl_url);

  // 本地 CRL 文件
  base::Result<void> LoadCRLFromFile(const std::string& filepath);

  // 检查证书是否被吊销
  bool IsRevoked(const std::string& serial_number) const;

  // 定期刷新 CRL
  void SetRefreshInterval(std::chrono::seconds interval);

  // 获取 CRL 信息
  struct CRLInfo {
    std::string issuer;
    std::chrono::system_clock::time_point this_update;
    std::chrono::system_clock::time_point next_update;
    size_t revoked_count;
  };
  std::optional<CRLInfo> GetInfo() const;

 private:
  void RefreshLoop();

  struct RevokedEntry {
    std::string serial_number;
    std::chrono::system_clock::time_point revocation_date;
  };

  std::vector<RevokedEntry> revoked_list_;
  mutable std::mutex mutex_;
  std::chrono::seconds refresh_interval_{3600};
  std::string current_crl_url_;
  std::atomic<bool> running_{false};
  std::thread refresh_thread_;
};

// ==================== mTLS 服务 ====================

struct MTLSServiceConfig {
  std::string ca_cert_path;              // CA 证书
  std::string server_cert_path;          // 服务器证书
  std::string server_key_path;           // 服务器私钥
  std::string dh_params_path;            // DH 参数（可选）
  bool require_client_cert = true;       // 强制客户端证书
  std::vector<std::string> allowed_cas;  // 允许的 CA 列表
  int min_tls_version = 4;               // 最低 TLS 版本 (1.2=4, 1.3=5)
  bool enable_ocsp_stapling = true;      // OCSP 装订

  // 证书绑定策略
  bool bind_to_device = false;  // 绑定到设备指纹
  int max_cert_age_days = 365;  // 证书最大有效期
};

class MTLSService {
 public:
  using Config = MTLSServiceConfig;

  explicit MTLSService(const Config& config = Config());
  ~MTLSService();

  // ========== 初始化 ==========

  base::Result<void> Initialize();
  SSL_CTX* GetSSLContext();  // 获取 OpenSSL 上下文

  // ========== 证书验证 ==========

  // 验证客户端证书
  ClientCertContext VerifyClientCertificate(SSL* ssl);
  ClientCertContext VerifyClientCertificate(X509* cert);

  // 验证结果 → 错误信息
  static std::string VerifyResultToString(CertVerifyResult result);

  // ========== 证书管理 ==========

  // 生成客户端证书（用于新用户）
  base::Result<std::pair<std::string, std::string>> IssueClientCertificate(int64_t player_id,
                                                                           const std::string& cn);

  // 吊销客户端证书
  base::Result<void> RevokeCertificate(const std::string& serial_number, const std::string& reason);

  // 黑名单管理
  void AddToBlacklist(const std::string& cert_fingerprint);
  bool IsBlacklisted(const std::string& cert_fingerprint) const;
  void RemoveFromBlacklist(const std::string& cert_fingerprint);

  // ========== 状态查询 ==========

  struct ServiceStats {
    int total_connections = 0;
    int verified_connections = 0;
    int failed_verifications = 0;
    std::unordered_map<CertVerifyResult, int> failure_reasons;
    int active_sessions = 0;
    int revoked_cert_checks = 0;
    int blacklisted_rejections = 0;
  };

  ServiceStats GetStats() const;
  bool IsInitialized() const { return ssl_ctx_ != nullptr; }

  // CRL 管理
  CRLManager& GetCRLManager() { return crl_manager_; }

 private:
  Config config_;
  SSL_CTX* ssl_ctx_ = nullptr;

  // 跟踪活动会话
  mutable std::mutex sessions_mutex_;
  std::unordered_map<std::string, ClientCertContext> active_sessions_;

  // 黑名单（吊销指纹）
  std::unordered_set<std::string> blacklist_;

  // CRL 管理器
  CRLManager crl_manager_;

  // 统计
  ServiceStats stats_;

  // 内部方法
  static int VerifyCallback(int preverify_ok, X509_STORE_CTX* ctx);
  CertificateInfo ExtractCertInfo(X509* cert) const;
  std::string ComputeFingerprint(X509* cert) const;
  std::string GenerateSerialNumber() const;
};

// ==================== 证书事件回调 ====================

struct CertEvent {
  enum class Type {
    CertIssued = 0,
    CertRevoked = 1,
    CertExpired = 2,
    CRLUpdated = 3,
    VerificationFailed = 4,
    Blacklisted = 5,
  };

  Type type;
  std::string serial_number;
  std::string player_id;
  std::string reason;
  std::chrono::system_clock::time_point timestamp;
};

using CertEventCallback = std::function<void(const CertEvent&)>;

}  // namespace poker_engine::security
