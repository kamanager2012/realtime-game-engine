#pragma once

#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace poker_engine::base {

enum class AuditEvent {
  Login = 1,
  Logout = 2,
  JoinTable = 3,
  LeaveTable = 4,
  ActionTaken = 5,
  HandResult = 6,
  CashTransaction = 7,
  Payout = 8,
  SystemConfigChange = 9,
  SuspiciousActivity = 10,
  AdminAction = 11,
};

struct AuditRecord {
  uint64_t id;
  AuditEvent event;
  int64_t player_id;
  std::string table_id;
  std::string details;
  std::string ip_address;
  std::string timestamp;
  std::string session_id;
};

class AuditLogger {
 public:
  static AuditLogger& Instance() {
    static AuditLogger instance;
    return instance;
  }

  bool Initialize(const std::string& filepath);
  void Shutdown();

  uint64_t Log(AuditEvent event, int64_t player_id, const std::string& table_id,
               const std::string& details_json, const std::string& ip_address = "",
               const std::string& session_id = "");

  std::vector<AuditRecord> Query(int64_t player_id, const std::string& start_time,
                                 const std::string& end_time, int limit = 100);

  bool VerifyIntegrity();

 private:
  AuditLogger() = default;
  ~AuditLogger();

  AuditLogger(const AuditLogger&) = delete;
  AuditLogger& operator=(const AuditLogger&) = delete;

  std::string filepath_;
  std::mutex mutex_;
  std::ofstream file_;
  uint64_t next_id_ = 1;
  uint64_t last_flush_id_ = 0;

  std::string GenerateTimestamp();
  std::string SanitizeJSON(const std::string& json);
  void Flush();
};

}  // namespace poker_engine::base
