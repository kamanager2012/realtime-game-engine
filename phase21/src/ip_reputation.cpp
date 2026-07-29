#include "poker_engine/security/ip_reputation.h"

#include <openssl/sha.h>

#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::security {

namespace {

int Levenshtein(const std::string& a, const std::string& b) {
  int m = a.size(), n = b.size();
  std::vector<int> prev(n + 1), curr(n + 1);
  for (int j = 0; j <= n; ++j) prev[j] = j;
  for (int i = 1; i <= m; ++i) {
    curr[0] = i;
    for (int j = 1; j <= n; ++j) {
      if (a[i - 1] == b[j - 1])
        curr[j] = prev[j - 1];
      else
        curr[j] = 1 + std::min({prev[j], curr[j - 1], prev[j - 1]});
    }
    std::swap(prev, curr);
  }
  return prev[n];
}

}  // namespace

// ==================== DeviceFingerprint ====================

std::string DeviceFingerprint::ComputeHash() const {
  std::string raw =
      user_agent + "|" + os + "|" + browser + "|" + canvas_hash + "|" + webgl_renderer;
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(raw.data()), raw.size(), hash);
  std::ostringstream oss;
  for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
  }
  return oss.str();
}

double DeviceFingerprint::Similarity(const DeviceFingerprint& a, const DeviceFingerprint& b) {
  if (a.fingerprint_hash == b.fingerprint_hash) return 1.0;

  double score = 0.0;
  double weights = 0.0;

  double ua_sim = 1.0 - static_cast<double>(Levenshtein(a.user_agent, b.user_agent)) /
                            std::max(a.user_agent.size(), b.user_agent.size());
  score += ua_sim * 0.3;
  weights += 0.3;

  if (a.os == b.os) score += 0.2;
  weights += 0.2;

  if (a.browser == b.browser) score += 0.2;
  weights += 0.2;

  if (a.canvas_hash == b.canvas_hash && !a.canvas_hash.empty())
    score += 0.2;
  else if (!a.canvas_hash.empty() && !b.canvas_hash.empty())
    score += 0.05;
  weights += 0.2;

  if (!a.timezone.empty() && !b.timezone.empty()) {
    int tz_diff = std::abs(std::stoi(a.timezone) - std::stoi(b.timezone));
    if (tz_diff == 0)
      score += 0.1;
    else if (tz_diff <= 2)
      score += 0.05;
  }
  weights += 0.1;

  return weights > 0 ? score / weights : 0.0;
}

// ==================== IPReputationManager ====================

IPReputationManager::IPReputationManager(size_t cache_size) : max_cache_size_(cache_size) {
  PE_LOG_INFO("IP Reputation Manager initialized (cache_size={})", cache_size);
}

IPReputation IPReputationManager::Query(const std::string& ip_address) const {
  std::shared_lock lock(cache_mutex_);
  auto it = cache_.find(ip_address);
  if (it != cache_.end()) {
    return it->second;
  }
  return ComputeBaseReputation(ip_address);
}

IPReputation IPReputationManager::ComputeBaseReputation(const std::string& ip_address) const {
  IPReputation rep;
  rep.ip_address = ip_address;
  rep.score = 50.0;

  if (ip_address.find("192.168.") == 0 || ip_address.find("10.") == 0 ||
      ip_address.find("127.0.0.") == 0 || ip_address.find("172.16.") == 0) {
    rep.is_datacenter = false;
    rep.score = 80.0;
  } else if (ip_address.find(".onion") != std::string::npos) {
    rep.is_tor_exit = true;
    rep.is_proxy = true;
    rep.score = 10.0;
    rep.threat_level = ThreatLevel::High;
    rep.tags = {"tor"};
  }

  return rep;
}

void IPReputationManager::ReportBehavior(const std::string& ip_address, ThreatLevel level,
                                         const std::string& reason) {
  {
    std::lock_guard<std::mutex> lock(log_mutex_);
    behavior_log_.push_back({ip_address, level, reason, std::chrono::system_clock::now()});
  }

  UpdateScore(ip_address, -static_cast<int>(level) * 10.0);

  PE_LOG_WARN("IP Reputation: {} reported as threat level {} - {}", ip_address,
              static_cast<int>(level), reason);
}

void IPReputationManager::BulkImport(const std::string& csv_path) {
  std::ifstream file(csv_path);
  if (!file.is_open()) {
    PE_LOG_ERROR("Failed to open IP reputation file: {}", csv_path);
    return;
  }

  std::string line;
  int imported = 0;

  while (std::getline(file, line)) {
    std::istringstream ss(line);
    IPReputation rep;
    std::string score_str, tags;

    if (std::getline(ss, rep.ip_address, ',') && std::getline(ss, score_str, ',') &&
        std::getline(ss, tags, ',')) {
      rep.score = std::stod(score_str);

      if (tags.find("proxy") != std::string::npos) rep.is_proxy = true;
      if (tags.find("tor") != std::string::npos) rep.is_tor_exit = true;
      if (tags.find("datacenter") != std::string::npos) rep.is_datacenter = true;

      if (rep.score < 20)
        rep.threat_level = ThreatLevel::Critical;
      else if (rep.score < 40)
        rep.threat_level = ThreatLevel::High;
      else if (rep.score < 60)
        rep.threat_level = ThreatLevel::Medium;
      else
        rep.threat_level = ThreatLevel::Low;

      {
        std::lock_guard<std::shared_mutex> lock(cache_mutex_);
        cache_[rep.ip_address] = std::move(rep);
      }
      imported++;
    }
  }

  PE_LOG_INFO("Imported {} IP reputation records from {}", imported, csv_path);
}

bool IPReputationManager::IsTrusted(const std::string& ip_address) const {
  auto rep = Query(ip_address);
  return rep.threat_level <= ThreatLevel::Low && rep.score >= 60.0;
}

bool IPReputationManager::ShouldBlock(const std::string& ip_address) const {
  auto rep = Query(ip_address);
  return rep.threat_level >= ThreatLevel::High || rep.score < 20.0;
}

std::vector<int64_t> IPReputationManager::GetAssociatedAccounts(const std::string& ip) const {
  std::lock_guard<std::mutex> lock(link_mutex_);

  std::vector<int64_t> accounts;
  auto range = ip_accounts_.equal_range(ip);
  for (auto it = range.first; it != range.second; ++it) {
    accounts.push_back(it->second.player_id);
  }
  return accounts;
}

typename IPReputationManager::Stats IPReputationManager::GetStats() const {
  std::shared_lock lock(cache_mutex_);

  Stats stats;
  stats.total_queried = cache_.size();
  stats.known_ips =
      std::count_if(cache_.begin(), cache_.end(), [](const auto& p) { return p.second.score > 0; });
  stats.blocked_ips = std::count_if(cache_.begin(), cache_.end(),
                                    [](const auto& p) { return p.second.score < 20.0; });
  stats.threats_detected = std::count_if(cache_.begin(), cache_.end(), [](const auto& p) {
    return p.second.threat_level >= ThreatLevel::High;
  });

  return stats;
}

void IPReputationManager::EvictIfNeeded() {
  if (cache_.size() >= max_cache_size_) {
    std::vector<std::string> to_remove;
    for (auto& [ip, rep] : cache_) {
      if (rep.score < 30.0) to_remove.push_back(ip);
    }
    for (auto& ip : to_remove) cache_.erase(ip);
    PE_LOG_INFO("Evicted {} low-reputation IPs from cache", to_remove.size());
  }
}

void IPReputationManager::UpdateScore(const std::string& ip, double delta) {
  std::unique_lock lock(cache_mutex_);

  auto it = cache_.find(ip);
  if (it == cache_.end()) {
    it = cache_.emplace(ip, ComputeBaseReputation(ip)).first;
  }

  auto& rep = it->second;
  rep.score = std::max(0.0, std::min(100.0, rep.score + delta));
  rep.last_seen = std::chrono::system_clock::now();

  if (rep.score < 20.0)
    rep.threat_level = ThreatLevel::Critical;
  else if (rep.score < 40.0)
    rep.threat_level = ThreatLevel::High;
  else if (rep.score < 60.0)
    rep.threat_level = ThreatLevel::Medium;
  else
    rep.threat_level = ThreatLevel::Low;
}

// ==================== DeviceFingerprintManager ====================

DeviceFingerprintManager::DeviceFingerprintManager() {
  PE_LOG_INFO("Device Fingerprint Manager initialized");
}

DeviceFingerprint DeviceFingerprintManager::Generate(const std::string& raw_data) {
  DeviceFingerprint fp;

  try {
    auto j = nlohmann::json::parse(raw_data);
    fp.user_agent = j.value("ua", "");
    fp.os = j.value("os", "");
    fp.browser = j.value("browser", "");
    fp.canvas_hash = j.value("canvas", "");
    fp.webgl_renderer = j.value("webgl", "");
    fp.audio_fingerprint = j.value("audio", "");
    fp.screen_resolution = j.value("screen", "");
    fp.timezone = j.value("tz", "");
    fp.language = j.value("lang", "");
  } catch (...) {
    fp.user_agent = raw_data;
  }

  fp.fingerprint_hash = fp.ComputeHash();
  return fp;
}

bool DeviceFingerprintManager::Validate(const DeviceFingerprint& fp) const {
  if (fp.fingerprint_hash.empty()) return false;
  if (fp.user_agent.empty()) return false;
  return true;
}

std::vector<typename DeviceFingerprintManager::SharingAlert>
DeviceFingerprintManager::DetectSharing() {
  std::shared_lock lock(map_mutex_);

  std::vector<SharingAlert> alerts;

  for (auto& [hash, info] : device_map_) {
    if (info.linked_accounts.size() >= 2) {
      alerts.push_back({hash, info.linked_accounts, 1.0, "Same device used by multiple accounts"});
    }
  }

  return alerts;
}

void DeviceFingerprintManager::LinkDevice(int64_t player_id, const DeviceFingerprint& fp) {
  std::lock_guard<std::shared_mutex> lock(map_mutex_);

  auto& info = device_map_[fp.fingerprint_hash];
  if (info.linked_accounts.empty()) {
    info.fingerprint = fp;
    info.first_seen = std::chrono::system_clock::now();
  }

  auto& accts = info.linked_accounts;
  if (std::find(accts.begin(), accts.end(), player_id) == accts.end()) {
    accts.push_back(player_id);
  }

  info.last_player_id = player_id;
}

std::vector<int64_t> DeviceFingerprintManager::GetAccountsForDevice(
    const std::string& fp_hash) const {
  std::shared_lock lock(map_mutex_);

  if (device_map_.count(fp_hash)) {
    return device_map_.at(fp_hash).linked_accounts;
  }
  return {};
}

}  // namespace poker_engine::security
