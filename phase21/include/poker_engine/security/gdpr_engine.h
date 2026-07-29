#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "poker_engine/base/result.h"

namespace poker_engine::security {

// ==================== GDPR 合规引擎 ====================
// 实现数据主体权利：
//   - 访问权 (Article 15)
//   - 更正权 (Article 16)
//   - 删除权/被遗忘权 (Article 17)
//   - 数据可携权 (Article 20)
//   - 限制处理权 (Article 18)
//   - 反对权 (Article 21)

enum class DataSubjectRequestType : uint8_t {
  Access = 0,         // 数据访问请求
  Rectification = 1,  // 数据更正
  Erasure = 2,        // 删除/被遗忘
  Portability = 3,    // 数据可携
  Restriction = 4,    // 限制处理
  Objection = 5,      // 反对处理
  Withdrawal = 6,     // 撤回同意
};

enum class RequestStatus : uint8_t {
  Pending = 0,
  InProgress = 1,
  Completed = 2,
  Rejected = 3,
  Escalated = 4,
};

// ==================== 数据主体请求记录 ====================

struct DataSubjectRequest {
  uint64_t request_id;
  int64_t player_id;
  DataSubjectRequestType type;
  RequestStatus status;
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point deadline;  // 30天法定期限
  std::string reason;                              // 请求描述
  std::string justification;                       // 处理理由
  std::string result_data;                         // 返回的数据 (用于访问/可携)
  std::string notes;                               // 处理备注
  std::string handler_id;                          // 处理人

  std::string StatusToString() const {
    switch (status) {
      case RequestStatus::Pending:
        return "Pending";
      case RequestStatus::InProgress:
        return "InProgress";
      case RequestStatus::Completed:
        return "Completed";
      case RequestStatus::Rejected:
        return "Rejected";
      case RequestStatus::Escalated:
        return "Escalated";
    }
    return "Unknown";
  }
};

// ==================== 数据分类 ====================

enum class DataCategory : uint8_t {
  PersonalIdentity = 0,  // 姓名、邮箱、电话
  Financial = 1,         // 筹码、投注、交易
  Behavioral = 2,        // 游戏行为、游戏记录
  Authentication = 3,    // 密码哈希、证书
  Communication = 4,     // 聊天记录、邮件
  Metadata = 5,          // IP、设备信息、日志
};

struct DataRecord {
  int64_t player_id;
  DataCategory category;
  std::string table_name;    // 源数据表
  std::string record_id;     // 源记录 ID
  std::string data_summary;  // 数据摘要（非完整数据）
  std::chrono::system_clock::time_point created_at;
  bool has_consent = true;  // 是否有处理许可
};

// ==================== 匿名化策略 ====================

enum class AnonymizationMethod : uint8_t {
  Delete = 0,        // 完全删除
  Anonymize = 1,     // 匿名化（保留统计信息）
  Pseudonymize = 2,  // 假名化（可关联）
  Aggregate = 3,     // 聚合（仅保留统计汇总）
};

struct AnonymizationRule {
  DataCategory category;
  AnonymizationMethod method;
  int retention_days;         // 数据保留天数
  bool requires_legal_basis;  // 是否需要法律依据
};

// ==================== GDPR 合规引擎 ====================

struct GDPRConfig {
  int default_response_days = 30;  // 法定响应期限（天）
  int data_retention_days = 365;   // 默认数据保留期
  bool auto_purge_expired = true;  // 自动清理过期数据
  bool require_consent_for_analytics = true;
  std::string dpo_contact;  // 数据保护官联系方式
};

class GDPRComplianceEngine {
 public:
  using Config = GDPRConfig;

  explicit GDPRComplianceEngine(const Config& config = Config());

  // ============ 数据主体权利 - 实现 ============

  // 1. 访问权 (Art. 15): 获取玩家所有个人数据
  base::Result<DataSubjectRequest> SubmitAccessRequest(int64_t player_id,
                                                       int64_t requested_by_player_id);

  // 2. 更正权 (Art. 16): 更新不正确的个人数据
  base::Result<void> RectifyData(int64_t player_id, DataCategory category, const std::string& field,
                                 const std::string& new_value);

  // 3. 删除权/被遗忘权 (Art. 17): 删除玩家的个人数据
  base::Result<DataSubjectRequest> SubmitErasureRequest(int64_t player_id,
                                                        const std::string& justification);

  // 4. 数据可携权 (Art. 20): 导出可移植格式
  base::Result<std::string> ExportDataPortability(int64_t player_id);

  // 5. 限制处理权 (Art. 18)
  base::Result<void> RestrictProcessing(int64_t player_id, DataCategory category);

  // 6. 撤回同意
  base::Result<void> WithdrawConsent(int64_t player_id);

  // ============ 请求管理 ============

  // 获取待处理请求
  std::vector<DataSubjectRequest> GetPendingRequests();

  // 处理请求
  base::Result<void> ProcessRequest(uint64_t request_id, bool approved,
                                    const std::string& handler_id, const std::string& notes = "");

  // 自动检查过期请求
  void CheckDeadlines();

  // ============ 数据生命周期管理 ============

  // 注册新数据记录
  void RegisterDataRecord(const DataRecord& record);

  // 同意管理
  void RecordConsent(int64_t player_id, DataCategory category, bool granted,
                     const std::string& purpose);
  bool HasConsent(int64_t player_id, DataCategory category) const;

  // 数据保留策略
  void ApplyRetentionPolicy();

  // 清理过期数据
  int PurgeExpiredData();

  // ============ 审计 ============

  // 获取数据处理活动日志 (Art. 30)
  std::string GetProcessingLog(int64_t player_id);

  // 违规通知 (Art. 33/34)
  base::Result<void> ReportBreach(const std::string& description,
                                  const std::vector<int64_t>& affected_players,
                                  const std::string& risk_level);

  // ============ 查询 ============

  bool IsPlayerDataRestricted(int64_t player_id) const;
  std::vector<DataRecord> GetPlayerDataRecords(int64_t player_id) const;
  Config GetConfig() const { return config_; }

 private:
  Config config_;

  // 数据主体请求
  std::unordered_map<uint64_t, DataSubjectRequest> requests_;
  std::atomic<uint64_t> next_request_id_{1};
  mutable std::mutex requests_mutex_;

  // 数据记录
  std::unordered_map<int64_t, std::vector<DataRecord>> player_data_records_;
  mutable std::mutex records_mutex_;

  // 同意记录
  struct ConsentRecord {
    DataCategory category;
    bool granted;
    std::chrono::system_clock::time_point granted_at;
    std::string purpose;
  };
  std::unordered_map<int64_t, std::vector<ConsentRecord>> consent_records_;
  mutable std::mutex consent_mutex_;

  // 处理限制
  std::unordered_map<int64_t, std::vector<DataCategory>> restrictions_;
  mutable std::mutex restrictions_mutex_;

  // 匿名化
  base::Result<void> AnonymizePlayerData(int64_t player_id, AnonymizationMethod method);

  base::Result<void> DeletePlayerData(int64_t player_id);

  base::Result<std::string> GeneratePortabilityData(int64_t player_id);
};

// ==================== 匿名化辅助 ====================

class DataAnonymizer {
 public:
  explicit DataAnonymizer(const std::vector<AnonymizationRule>& rules);

  // 匿名化玩家数据
  base::Result<void> Anonymize(int64_t player_id, AnonymizationMethod method);

  // 验证匿名化效果
  bool VerifyAnonymization(int64_t player_id);

  // 获取匿名化规则
  const std::vector<AnonymizationRule>& GetRules() const { return rules_; }

 private:
  std::vector<AnonymizationRule> rules_;

  void HashPseudoField(const std::string& input, std::string& output);
  void GeneralizeField(const std::string& input, std::string& output, int level);
  void PerturbNumericField(double& value, double epsilon);
};

}  // namespace poker_engine::security
