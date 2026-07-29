#pragma once

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <thread>

#include "poker_engine/base/audit_logger.h"

namespace poker_engine::base {

// ==================== 结构化安全审计日志 ====================
// JSON Lines 格式，每行一条记录
// 兼容 SIEM 系统（如 ELK、Splunk、Graylog）

struct AuditLogEntry {
  uint64_t seq_id;                 // 全局递增序列号
  std::string timestamp;           // ISO 8601 UTC
  AuditEvent event_type;           // 事件类型
  int64_t player_id;               // 玩家 ID
  std::string table_id;            // 牌桌 ID
  std::string session_id;          // 会话 ID
  std::string source_ip;           // 来源 IP
  std::string device_fingerprint;  // 设备指纹
  nlohmann::json details;          // 结构化详情
  int risk_score;                  // 风险评分 (0-100)
  std::string action_taken;        // 采取的措施
  std::string status;              // success/failed/reviewed

  std::string Serialize() const {
    nlohmann::json j;
    j["seq"] = seq_id;
    j["ts"] = timestamp;
    j["event"] = static_cast<int>(event_type);
    j["event_name"] = EventName(event_type);
    j["player_id"] = player_id;
    j["table"] = table_id;
    j["session"] = session_id;
    j["ip"] = source_ip;
    j["device"] = device_fingerprint;
    j["details"] = details;
    j["risk"] = risk_score;
    j["action"] = action_taken;
    j["status"] = status;
    return j.dump();
  }

  static std::optional<AuditLogEntry> Deserialize(const std::string& json_str) {
    try {
      auto j = nlohmann::json::parse(json_str);
      AuditLogEntry entry;
      entry.seq_id = j.value("seq", 0);
      entry.timestamp = j.value("ts", "");
      entry.event_type = static_cast<AuditEvent>(j.value("event", 0));
      entry.player_id = j.value("player_id", 0);
      entry.table_id = j.value("table", "");
      entry.session_id = j.value("session", "");
      entry.source_ip = j.value("ip", "");
      entry.device_fingerprint = j.value("device", "");
      entry.details = j.value("details", nlohmann::json::object());
      entry.risk_score = j.value("risk", 0);
      entry.action_taken = j.value("action", "");
      entry.status = j.value("status", "");
      return entry;
    } catch (...) {
      return std::nullopt;
    }
  }

 private:
  static std::string EventName(AuditEvent e) {
    switch (e) {
      case AuditEvent::Login:
        return "login";
      case AuditEvent::Logout:
        return "logout";
      case AuditEvent::JoinTable:
        return "join_table";
      case AuditEvent::LeaveTable:
        return "leave_table";
      case AuditEvent::ActionTaken:
        return "action";
      case AuditEvent::HandResult:
        return "hand_result";
      case AuditEvent::CashTransaction:
        return "cash_transaction";
      case AuditEvent::Payout:
        return "payout";
      case AuditEvent::SystemConfigChange:
        return "config_change";
      case AuditEvent::SuspiciousActivity:
        return "suspicious_activity";
      case AuditEvent::AdminAction:
        return "admin_action";
    }
    return "unknown";
  }
};

// ==================== 异步审计日志器 ====================
// 使用后台线程批量写入，减少对主线程的影响

class StructuredAuditLogger {
 public:
  static StructuredAuditLogger& Instance() {
    static StructuredAuditLogger instance;
    return instance;
  }

  bool Initialize(const std::string& filepath, const std::string& rotation_policy = "daily");

  // 异步记录（不阻塞调用线程）
  uint64_t LogAsync(const AuditLogEntry& entry);

  // 同步记录（等待写入完成）
  uint64_t LogSync(const AuditLogEntry& entry);

  // 查询接口
  std::vector<AuditLogEntry> QueryByPlayer(int64_t player_id, int limit = 100) const;

  std::vector<AuditLogEntry> QueryByType(AuditEvent event_type, int limit = 100) const;

  std::vector<AuditLogEntry> QueryByTimeRange(const std::string& start, const std::string& end,
                                              int limit = 500) const;

  // 完整性验证
  bool VerifyIntegrity() const;

  // 优雅关闭（确保所有缓冲数据写入磁盘）
  void Shutdown();

  // 统计
  size_t TotalEntries() const { return entry_counter_.load(); }
  size_t PendingEntries() const;

 private:
  StructuredAuditLogger() = default;
  ~StructuredAuditLogger() { Shutdown(); }

  StructuredAuditLogger(const StructuredAuditLogger&) = delete;
  StructuredAuditLogger& operator=(const StructuredAuditLogger&) = delete;

  void WriteLoop();
  void RotateFileIfNeeded();
  std::string GenerateFilename() const;

  std::string base_dir_;
  std::string current_file_;
  std::string rotation_policy_;

  mutable std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::queue<AuditLogEntry> pending_queue_;

  std::ofstream current_stream_;
  std::atomic<uint64_t> entry_counter_{0};
  std::atomic<bool> running_{false};
  std::atomic<bool> shutdown_requested_{false};
  std::thread writer_thread_;

  mutable std::mutex file_mutex_;
  size_t current_file_size_ = 0;
  size_t max_file_size_ = 100 * 1024 * 1024;  // 100MB
};

// ==================== 便捷宏 ====================

#define STRUCTURED_AUDIT(event_type, player_id, details_json, ...)         \
  do {                                                                     \
    poker_engine::base::AuditLogEntry entry;                               \
    entry.seq_id = 0;                                                      \
    entry.event_type = (event_type);                                       \
    entry.player_id = (player_id);                                         \
    entry.timestamp = poker_engine::base::GenerateISOTimestamp();          \
    entry.details = nlohmann::json::parse(details_json);                   \
    entry.risk_score = 0;                                                  \
    entry.status = "pending";                                              \
    __VA_ARGS__;                                                           \
    poker_engine::base::StructuredAuditLogger::Instance().LogAsync(entry); \
  } while (0)

std::string GenerateISOTimestamp();

}  // namespace poker_engine::base
