#include "poker_engine/base/structured_audit_logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::base {

std::string GenerateISOTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0')
      << std::setw(3) << ms.count() << 'Z';
  return oss.str();
}

bool StructuredAuditLogger::Initialize(const std::string& filepath,
                                       const std::string& rotation_policy) {
  base_dir_ = filepath;
  rotation_policy_ = rotation_policy;
  std::filesystem::create_directories(base_dir_);

  for (const auto& entry : std::filesystem::directory_iterator(base_dir_)) {
    if (entry.path().extension() == ".audit") {
      current_file_size_ += std::filesystem::file_size(entry.path());
    }
  }

  current_file_ = GenerateFilename();
  current_stream_.open(current_file_, std::ios::app);

  if (!current_stream_.is_open()) {
    PE_LOG_ERROR("Failed to open audit log file: {}", current_file_);
    return false;
  }

  if (!VerifyIntegrity()) {
    PE_LOG_ERROR("Audit log integrity check failed!");
    return false;
  }

  running_ = true;
  writer_thread_ = std::thread(&StructuredAuditLogger::WriteLoop, this);

  PE_LOG_INFO("Structured audit logger initialized: {}", current_file_);
  return true;
}

uint64_t StructuredAuditLogger::LogAsync(const AuditLogEntry& entry) {
  uint64_t seq = entry_counter_.fetch_add(1) + 1;
  AuditLogEntry mutable_entry = entry;
  mutable_entry.seq_id = seq;

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pending_queue_.push(std::move(mutable_entry));
  }
  queue_cv_.notify_one();
  return seq;
}

uint64_t StructuredAuditLogger::LogSync(const AuditLogEntry& entry) {
  uint64_t seq = entry_counter_.fetch_add(1) + 1;
  AuditLogEntry mutable_entry = entry;
  mutable_entry.seq_id = seq;

  {
    std::lock_guard<std::mutex> lock(file_mutex_);
    current_stream_ << mutable_entry.Serialize() << "\n";
    current_stream_.flush();
    current_file_size_ += mutable_entry.Serialize().size() + 1;
  }

  RotateFileIfNeeded();
  return seq;
}

std::vector<AuditLogEntry> StructuredAuditLogger::QueryByPlayer(int64_t player_id,
                                                                int limit) const {
  std::vector<AuditLogEntry> results;

  for (const auto& entry : std::filesystem::directory_iterator(base_dir_)) {
    if (entry.path().extension() != ".audit") continue;
    std::ifstream ifs(entry.path());
    std::string line;

    while (std::getline(ifs, line) && results.size() < static_cast<size_t>(limit)) {
      auto parsed = AuditLogEntry::Deserialize(line);
      if (parsed && parsed->player_id == player_id) {
        results.push_back(std::move(*parsed));
      }
    }
  }

  std::sort(results.begin(), results.end(),
            [](const auto& a, const auto& b) { return a.seq_id > b.seq_id; });

  if (results.size() > static_cast<size_t>(limit)) results.resize(limit);
  return results;
}

bool StructuredAuditLogger::VerifyIntegrity() const {
  uint64_t last_seq = 0;

  for (const auto& entry : std::filesystem::directory_iterator(base_dir_)) {
    if (entry.path().extension() != ".audit") continue;
    std::ifstream ifs(entry.path());
    std::string line;

    while (std::getline(ifs, line)) {
      if (line.empty()) continue;
      try {
        auto parsed = AuditLogEntry::Deserialize(line);
        if (!parsed) return false;
        if (parsed->seq_id <= last_seq) return false;
        last_seq = parsed->seq_id;
      } catch (...) {
        return false;
      }
    }
  }
  return true;
}

void StructuredAuditLogger::Shutdown() {
  shutdown_requested_ = true;
  running_ = false;
  queue_cv_.notify_all();

  if (writer_thread_.joinable()) writer_thread_.join();

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!pending_queue_.empty()) {
      auto entry = std::move(pending_queue_.front());
      pending_queue_.pop();
      std::lock_guard<std::mutex> flock(file_mutex_);
      current_stream_ << entry.Serialize() << "\n";
    }
    current_stream_.flush();
  }

  if (current_stream_.is_open()) current_stream_.close();
  PE_LOG_INFO("Structured audit logger shutdown, entries={}", entry_counter_.load());
}

size_t StructuredAuditLogger::PendingEntries() const {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  return pending_queue_.size();
}

void StructuredAuditLogger::WriteLoop() {
  while (running_ || !pending_queue_.empty()) {
    std::vector<AuditLogEntry> batch;

    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait_for(lock, std::chrono::milliseconds(100),
                         [this]() { return !pending_queue_.empty() || shutdown_requested_; });

      size_t target = std::min(pending_queue_.size(), size_t(1000));
      while (target-- > 0 && !pending_queue_.empty()) {
        batch.push_back(std::move(pending_queue_.front()));
        pending_queue_.pop();
      }
    }

    if (batch.empty()) continue;

    {
      std::lock_guard<std::mutex> flock(file_mutex_);
      for (auto& entry : batch) {
        current_stream_ << entry.Serialize() << "\n";
        current_file_size_ += entry.Serialize().size() + 1;
      }
      current_stream_.flush();
    }

    RotateFileIfNeeded();
  }
}

void StructuredAuditLogger::RotateFileIfNeeded() {
  if (current_file_size_ >= max_file_size_) {
    current_stream_.close();
    current_file_ = GenerateFilename();
    current_stream_.open(current_file_, std::ios::app);
    current_file_size_ = 0;
    PE_LOG_INFO("Audit log rotated to {}", current_file_);
  }
}

std::string StructuredAuditLogger::GenerateFilename() const {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);

  std::ostringstream oss;
  oss << base_dir_ << "/audit_" << std::put_time(std::localtime(&t), "%Y%m%d_%H%M%S") << ".audit";
  return oss.str();
}

}  // namespace poker_engine::base
