// mTLS integration helpers for WebSocketServer
// Adapted for phase13 WebSocketServer interface (void* ws, ConnectionData)

#include <openssl/ssl.h>

#include "poker_engine/base/logging.h"
#include "poker_engine/network/websocket_server.h"
#include "poker_engine/security/ip_reputation.h"
#include "poker_engine/security/mtls_service.h"
#include "poker_engine/security/security_policy_engine.h"

namespace poker_engine::network {

// ==================== mTLS 辅助类 ====================
// 独立于 WebSocketServer 内部实现，通过公共接口集成

class MTLSHelper {
 public:
  explicit MTLSHelper(std::unique_ptr<security::MTLSService> service)
      : mtls_service_(std::move(service)) {}

  // 初始化 mTLS 服务
  base::Result<void> Initialize() {
    if (!mtls_service_) {
      return base::Result<void>::Err(base::MakeErrorCode(base::Error::InternalError));
    }
    return mtls_service_->Initialize();
  }

  security::MTLSService& Service() { return *mtls_service_; }
  const security::MTLSService& Service() const { return *mtls_service_; }

  // 验证客户端证书（从 SSL 连接提取）
  security::ClientCertContext VerifyClient(SSL* ssl) {
    if (!mtls_service_) {
      security::ClientCertContext ctx{};
      ctx.verify_result = security::CertVerifyResult::InvalidSignature;
      return ctx;
    }
    return mtls_service_->VerifyClientCertificate(ssl);
  }

  // 根据验证结果生成关闭码和原因
  static std::pair<int, std::string> MapVerifyResultToCloseCode(security::CertVerifyResult result) {
    switch (result) {
      case security::CertVerifyResult::OK:
        return {0, ""};
      case security::CertVerifyResult::Expired:
        return {4001, "Certificate expired"};
      case security::CertVerifyResult::Revoked:
        return {4002, "Certificate revoked"};
      case security::CertVerifyResult::UnknownCA:
        return {4003, "Unknown certificate authority"};
      case security::CertVerifyResult::Blacklisted:
        return {4004, "Certificate blacklisted"};
      default:
        return {4005, "Certificate verification failed"};
    }
  }

  // 从证书 CN 提取玩家 ID
  static std::string ExtractPlayerFromCertCN(const std::string& cn) {
    const std::string prefix = "poker_player_";
    size_t pos = cn.find(prefix);
    if (pos == std::string::npos) return "";
    return cn.substr(pos + prefix.size());
  }

  bool IsInitialized() const { return mtls_service_ && mtls_service_->IsInitialized(); }

 private:
  std::unique_ptr<security::MTLSService> mtls_service_;
};

// ==================== 安全策略辅助 ====================

class SecurityHelper {
 public:
  explicit SecurityHelper(security::SecurityPolicyEngine* engine) : policy_engine_(engine) {}

  // 评估连接风险
  security::SecurityPolicyEngine::Decision EvaluateConnection(const std::string& ip_address) {
    if (!policy_engine_) {
      return security::SecurityPolicyEngine::Decision{
          security::SecurityPolicyEngine::ActionType::Allow,
          1.0,
          "No policy engine configured",
          {}};
    }

    security::DeviceFingerprint fp;
    auto assessment = policy_engine_->EvaluateConnection(ip_address, fp, nullptr);
    return policy_engine_->MakeDecision(assessment);
  }

  // 报告威胁行为
  void ReportThreat(const std::string& ip, security::ThreatLevel level, const std::string& reason) {
    if (policy_engine_) {
      policy_engine_->ReportIPBehavior(ip, level, reason);
    }
  }

  // 根据决策记录日志
  static void LogDecision(int64_t player_id,
                          const security::SecurityPolicyEngine::Decision& decision) {
    switch (decision.action) {
      case security::SecurityPolicyEngine::ActionType::Allow:
        break;
      case security::SecurityPolicyEngine::ActionType::Monitor:
        PE_LOG_WARN("Security: Player {} under enhanced monitoring", player_id);
        break;
      case security::SecurityPolicyEngine::ActionType::Flag:
        PE_LOG_WARN("Security: Player {} flagged (confidence={})", player_id, decision.confidence);
        break;
      case security::SecurityPolicyEngine::ActionType::Kick:
        PE_LOG_WARN("Security: Player {} auto-kicked (confidence={})", player_id,
                    decision.confidence);
        break;
      case security::SecurityPolicyEngine::ActionType::Ban:
        PE_LOG_ERROR("Security: Player {} auto-BANNED (confidence={})", player_id,
                     decision.confidence);
        break;
    }
  }

 private:
  security::SecurityPolicyEngine* policy_engine_;
};

}  // namespace poker_engine::network
