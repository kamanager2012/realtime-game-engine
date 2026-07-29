#include "poker_engine/base/audit_logger.h"

#include <filesystem>
#include <iomanip>

#include "poker_engine/base/logging.h"

namespace poker_engine::base {

AuditLogger::~AuditLogger() { Shutdown(); }

bool AuditLogger::Initialize(const std::string& filepath) {
  std::lock_guard<std::mutex> lock(mutex_);

  filepath_ = filepath;

  std::filesystem::create_directories(std::filesystem::path(filepath).parent_path());

  if (std::filesystem::exists(filepath_)) {
    if (!VerifyIntegrity()) {
      PE_LOG_ERROR("Audit log integrity check failed: {}", filepath_);
      return false;
    }
  }

  file_.open(filepath_, std::ios::app);
  if (!file_.is_open()) {
    PE_LOG_ERROR("Failed to open audit log: {}", filepath_);
    return false;
  }

  // Compute next ID from existing file
  std::ifstream in(filepath_);
  std::string line;
  uint64_t max_id = 0;
  while (std::getline(in, line)) {
    if (!line.empty() && line[0] == '|') {
      try {
        size_t pos1 = line.find('|', 1);
        if (pos1 != std::string::npos) {
          uint64_t id = std::stoull(line.substr(1, pos1 - 1));
          max_id = std::max(max_id, id);
        }
      } catch (...) {
      }
    }
  }
  next_id_ = max_id + 1;

  PE_LOG_INFO("Audit logger initialized: {}, next_id={}", filepath_, next_id_);
  return true;
}

void AuditLogger::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  Flush();
  if (file_.is_open()) {
    file_.close();
  }
}

uint64_t AuditLogger::Log(AuditEvent event, int64_t player_id, const std::string& table_id,
                          const std::string& details_json, const std::string& ip_address,
                          const std::string& session_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  uint64_t id = next_id_++;
  std::string timestamp = GenerateTimestamp();
  std::string safe_details = SanitizeJSON(details_json);

  file_ << "|" << id << "|" << timestamp << "|" << static_cast<int>(event) << "|" << player_id
        << "|" << table_id << "|" << safe_details << "|" << ip_address << "|" << session_id
        << "|\n";

  if (!file_.good()) {
    PE_LOG_ERROR("Audit log write failed!");
  }

  last_flush_id_ = id;

  if (id % 1000 == 0) {
    Flush();
  }

  return id;
}

bool AuditLogger::VerifyIntegrity() {
  std::ifstream in(filepath_);
  std::string line;
  uint64_t last_id = 0;
  int errors = 0;

  while (std::getline(in, line)) {
    if (line.empty() || line[0] != '|') continue;

    size_t pos = 1;
    try {
      size_t end = line.find('|', pos);
      uint64_t id = std::stoull(line.substr(pos, end - pos));

      if (id <= last_id) {
        errors++;
        PE_LOG_ERROR("Audit log: ID not monotonic at line id={}", id);
      }
      last_id = id;
    } catch (...) {
      errors++;
    }
  }

  if (errors > 0) {
    PE_LOG_ERROR("Audit log integrity: {} errors found", errors);
    return false;
  }
  return true;
}

std::vector<AuditRecord> AuditLogger::Query(int64_t player_id, const std::string& start_time,
                                            const std::string& end_time, int limit) {
  std::ifstream in(filepath_);
  std::string line;
  std::vector<AuditRecord> results;

  while (std::getline(in, line) && results.size() < static_cast<size_t>(limit)) {
    if (line.empty() || line[0] != '|') continue;

    std::vector<std::string> parts;
    size_t start = 1;
    while (start < line.size()) {
      size_t end = line.find('|', start);
      if (end == std::string::npos) break;
      parts.push_back(line.substr(start, end - start));
      start = end + 1;
    }

    if (parts.size() < 6) continue;

    try {
      int64_t pid = std::stoll(parts[3]);
      if (pid != player_id) continue;

      std::string& ts = parts[1];
      if (!start_time.empty() && ts < start_time) continue;
      if (!end_time.empty() && ts > end_time) continue;

      AuditRecord rec;
      rec.id = std::stoull(parts[0]);
      rec.event = static_cast<AuditEvent>(std::stoi(parts[2]));
      rec.player_id = pid;
      rec.table_id = parts[4];
      rec.details = parts[5];
      rec.ip_address = parts.size() > 6 ? parts[6] : "";
      rec.session_id = parts.size() > 7 ? parts[7] : "";
      rec.timestamp = ts;

      results.push_back(rec);

    } catch (...) {
      continue;
    }
  }

  return results;
}

std::string AuditLogger::GenerateTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);

  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

std::string AuditLogger::SanitizeJSON(const std::string& json) {
  std::string sanitized;
  sanitized.reserve(json.size());

  for (char c : json) {
    if (c == '|' || c == '\n' || c == '\r') {
      sanitized += '?';
    } else if (static_cast<unsigned char>(c) < 0x20) {
      // Remove control characters
    } else {
      sanitized += c;
    }
  }

  return sanitized;
}

void AuditLogger::Flush() {
  if (file_.is_open()) {
    file_.flush();
  }
}

}  // namespace poker_engine::base
