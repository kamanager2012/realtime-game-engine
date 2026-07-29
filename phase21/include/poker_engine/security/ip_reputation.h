#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "poker_engine/base/result.h"

namespace poker_engine::security {

// ==================== IP 信誉评分 ====================

enum class ThreatLevel : uint8_t {
  None = 0,
  Low = 1,
  Medium = 2,
  High = 3,
  Critical = 4,
};

struct IPReputation {
  std::string ip_address;
  ThreatLevel threat_level = ThreatLevel::None;
  double score = 100.0;  // 0-100, 越高越可信
  int abuse_reports = 0;
  bool is_tor_exit = false;
  bool is_proxy = false;
  bool is_datacenter = false;
  bool is_known_attacker = false;
  std::string country;
  std::string as_org;  // ISP/AS 组织
  std::chrono::system_clock::time_point last_seen;
  std::vector<std::string> tags;  // "bot", "vpn", "tor", "abuser" 等

  double RiskFactor() const {
    double risk = 0.0;
    if (is_tor_exit) risk += 30.0;
    if (is_proxy) risk += 20.0;
    if (is_datacenter) risk += 15.0;
    if (is_known_attacker) risk += 40.0;
    risk += (100.0 - score);
    risk += abuse_reports * 10.0;
    return std::min(100.0, risk);
  }
};

// ==================== 设备指纹 ====================

struct DeviceFingerprint {
  std::string fingerprint_hash;
  std::string user_agent;
  std::string os;
  std::string browser;
  std::string screen_resolution;
  std::string timezone;
  std::string language;
  std::string canvas_hash;
  std::string webgl_renderer;
  std::string audio_fingerprint;

  std::string ComputeHash() const;

  // 计算两个指纹的相似度 (0-1)
  static double Similarity(const DeviceFingerprint& a, const DeviceFingerprint& b);
};

// ==================== IP 信誉管理器 ====================

class IPReputationManager {
 public:
  explicit IPReputationManager(size_t cache_size = 100000);

  // 查询 IP 信誉
  IPReputation Query(const std::string& ip_address) const;

  // 更新 IP 信誉（基于行为分析）
  void ReportBehavior(const std::string& ip_address, ThreatLevel level, const std::string& reason);

  // 批量导入信誉数据
  void BulkImport(const std::string& csv_path);

  // 检查 IP 是否可信
  bool IsTrusted(const std::string& ip_address) const;

  // 检查 IP 是否应该被阻止
  bool ShouldBlock(const std::string& ip_address) const;

  // 获取 IP 关联的账户列表
  std::vector<int64_t> GetAssociatedAccounts(const std::string& ip) const;

  // 统计
  struct Stats {
    size_t total_queried;
    size_t known_ips;
    size_t blocked_ips;
    size_t threats_detected;
  };
  Stats GetStats() const;

 private:
  std::unordered_map<std::string, IPReputation> cache_;
  mutable std::shared_mutex cache_mutex_;
  size_t max_cache_size_;

  // 行为日志
  struct BehaviorLog {
    std::string ip;
    ThreatLevel level;
    std::string reason;
    std::chrono::system_clock::time_point timestamp;
  };
  std::vector<BehaviorLog> behavior_log_;
  std::mutex log_mutex_;

  // IP 关联分析
  struct IPAccountLink {
    int64_t player_id;
    int connection_count;
    std::chrono::system_clock::time_point first_seen;
    std::chrono::system_clock::time_point last_seen;
  };
  std::unordered_multimap<std::string, IPAccountLink> ip_accounts_;
  mutable std::mutex link_mutex_;

  void EvictIfNeeded();
  void UpdateScore(const std::string& ip, double delta);
  IPReputation ComputeBaseReputation(const std::string& ip_address) const;
};

// ==================== 设备指纹管理器 ====================

class DeviceFingerprintManager {
 public:
  explicit DeviceFingerprintManager();

  // 生成/验证设备指纹
  DeviceFingerprint Generate(const std::string& raw_data);
  bool Validate(const DeviceFingerprint& fp) const;

  // 检测可疑的指纹共享（多个账户同一设备）
  struct SharingAlert {
    std::string device_hash;
    std::vector<int64_t> player_ids;
    double similarity_score;
    std::string reason;
  };

  std::vector<SharingAlert> DetectSharing();

  // 关联设备到账户
  void LinkDevice(int64_t player_id, const DeviceFingerprint& fp);

  // 检查设备是否已关联到其他账户
  std::vector<int64_t> GetAccountsForDevice(const std::string& fp_hash) const;

 private:
  struct DeviceInfo {
    DeviceFingerprint fingerprint;
    std::vector<int64_t> linked_accounts;
    std::chrono::system_clock::time_point first_seen;
    int64_t last_player_id;
  };

  std::unordered_map<std::string, DeviceInfo> device_map_;
  mutable std::shared_mutex map_mutex_;
};

}  // namespace poker_engine::security
