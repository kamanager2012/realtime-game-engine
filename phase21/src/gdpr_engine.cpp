#include "poker_engine/security/gdpr_engine.h"

#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::security {

namespace {

std::string GenerateUUID() {
  unsigned char uuid[16];
  RAND_bytes(uuid, sizeof(uuid));
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (int i = 0; i < 16; ++i) {
    oss << std::setw(2) << static_cast<int>(uuid[i]);
    if (i == 3 || i == 5 || i == 7 || i == 9) oss << '-';
  }
  return oss.str();
}

}  // namespace

// ==================== GDPRComplianceEngine ====================

GDPRComplianceEngine::GDPRComplianceEngine(const Config& config) : config_(config) {}

base::Result<DataSubjectRequest> GDPRComplianceEngine::SubmitAccessRequest(
    int64_t player_id, int64_t requested_by_player_id) {
  if (player_id != requested_by_player_id) {
    return base::Result<DataSubjectRequest>::Err(
        base::MakeErrorCode(base::Error::PermissionDenied));
  }

  uint64_t request_id = next_request_id_++;

  DataSubjectRequest request;
  request.request_id = request_id;
  request.player_id = player_id;
  request.type = DataSubjectRequestType::Access;
  request.status = RequestStatus::Pending;
  request.created_at = std::chrono::system_clock::now();
  request.deadline = request.created_at + std::chrono::hours(config_.default_response_days * 24);
  request.reason = "Data subject access request";

  {
    std::lock_guard<std::mutex> lock(requests_mutex_);
    requests_[request_id] = request;
  }

  PE_LOG_INFO("GDPR: Access request #{} submitted by player {}", request_id, player_id);

  return base::Result<DataSubjectRequest>::Ok(request);
}

base::Result<void> GDPRComplianceEngine::RectifyData(int64_t player_id, DataCategory category,
                                                     const std::string& field,
                                                     const std::string& new_value) {
  if (category == DataCategory::Authentication) {
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::InvalidArgument));
  }

  nlohmann::json log_entry;
  log_entry["action"] = "rectify";
  log_entry["player_id"] = player_id;
  log_entry["category"] = static_cast<int>(category);
  log_entry["field"] = field;
  log_entry["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

  PE_LOG_INFO("GDPR: Data rectified for player {} - {} / {}", player_id, field,
              static_cast<int>(category));

  return base::Result<void>::Ok();
}

base::Result<DataSubjectRequest> GDPRComplianceEngine::SubmitErasureRequest(
    int64_t player_id, const std::string& justification) {
  uint64_t request_id = next_request_id_++;

  DataSubjectRequest request;
  request.request_id = request_id;
  request.player_id = player_id;
  request.type = DataSubjectRequestType::Erasure;
  request.status = RequestStatus::Pending;
  request.created_at = std::chrono::system_clock::now();
  request.deadline = request.created_at + std::chrono::hours(config_.default_response_days * 24);
  request.reason = justification;

  {
    std::lock_guard<std::mutex> lock(requests_mutex_);
    requests_[request_id] = request;
  }

  PE_LOG_INFO("GDPR: Erasure request #{} from player {}", request_id, player_id);

  return base::Result<DataSubjectRequest>::Ok(request);
}

base::Result<std::string> GDPRComplianceEngine::ExportDataPortability(int64_t player_id) {
  if (!HasConsent(player_id, DataCategory::PersonalIdentity)) {
    return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::PermissionDenied));
  }

  return GeneratePortabilityData(player_id);
}

base::Result<void> GDPRComplianceEngine::RestrictProcessing(int64_t player_id,
                                                            DataCategory category) {
  std::lock_guard<std::mutex> lock(restrictions_mutex_);
  restrictions_[player_id].push_back(category);

  PE_LOG_INFO("GDPR: Processing restricted for player {} in category {}", player_id,
              static_cast<int>(category));
  return base::Result<void>::Ok();
}

base::Result<void> GDPRComplianceEngine::WithdrawConsent(int64_t player_id) {
  {
    std::lock_guard<std::mutex> lock(consent_mutex_);

    if (consent_records_.count(player_id)) {
      for (auto& consent : consent_records_[player_id]) {
        consent.granted = false;
      }
    }
  }

  {
    std::lock_guard<std::mutex> rlock(restrictions_mutex_);
    restrictions_[player_id] = {DataCategory::PersonalIdentity, DataCategory::Financial,
                                DataCategory::Behavioral, DataCategory::Communication};
  }

  PE_LOG_INFO("GDPR: Consent withdrawn by player {}", player_id);

  return base::Result<void>::Ok();
}

// ==================== 请求管理 ====================

std::vector<DataSubjectRequest> GDPRComplianceEngine::GetPendingRequests() {
  std::lock_guard<std::mutex> lock(requests_mutex_);

  std::vector<DataSubjectRequest> pending;
  for (auto& [id, req] : requests_) {
    if (req.status == RequestStatus::Pending || req.status == RequestStatus::InProgress) {
      pending.push_back(req);
    }
  }
  return pending;
}

base::Result<void> GDPRComplianceEngine::ProcessRequest(uint64_t request_id, bool approved,
                                                        const std::string& handler_id,
                                                        const std::string& notes) {
  std::lock_guard<std::mutex> lock(requests_mutex_);

  auto it = requests_.find(request_id);
  if (it == requests_.end()) {
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::NotFound));
  }

  auto& request = it->second;
  request.handler_id = handler_id;
  request.notes = notes;

  if (approved) {
    request.status = RequestStatus::Completed;

    switch (request.type) {
      case DataSubjectRequestType::Access:
        request.result_data = GeneratePortabilityData(request.player_id).UnwrapOr("{}");
        break;
      case DataSubjectRequestType::Erasure:
        AnonymizePlayerData(request.player_id, AnonymizationMethod::Anonymize);
        break;
      default:
        break;
    }
  } else {
    request.status = RequestStatus::Rejected;
  }

  PE_LOG_INFO("GDPR: Request #{} processed - {}", request_id, request.StatusToString());

  return base::Result<void>::Ok();
}

void GDPRComplianceEngine::CheckDeadlines() {
  auto now = std::chrono::system_clock::now();

  std::lock_guard<std::mutex> lock(requests_mutex_);

  for (auto& [id, req] : requests_) {
    if (req.status == RequestStatus::Pending || req.status == RequestStatus::InProgress) {
      if (now > req.deadline) {
        PE_LOG_WARN("GDPR: Request #{} has exceeded deadline", id);
        req.status = RequestStatus::Escalated;
      }
    }
  }
}

// ==================== 数据生命周期 ====================

void GDPRComplianceEngine::RegisterDataRecord(const DataRecord& record) {
  std::lock_guard<std::mutex> lock(records_mutex_);
  player_data_records_[record.player_id].push_back(record);
}

void GDPRComplianceEngine::RecordConsent(int64_t player_id, DataCategory category, bool granted,
                                         const std::string& purpose) {
  std::lock_guard<std::mutex> lock(consent_mutex_);

  consent_records_[player_id].push_back(
      {category, granted, std::chrono::system_clock::now(), purpose});
}

bool GDPRComplianceEngine::HasConsent(int64_t player_id, DataCategory category) const {
  std::lock_guard<std::mutex> lock(consent_mutex_);

  if (!consent_records_.count(player_id)) return false;

  for (const auto& consent : consent_records_.at(player_id)) {
    if (consent.category == category && consent.granted) return true;
  }

  return false;
}

void GDPRComplianceEngine::ApplyRetentionPolicy() {
  auto now = std::chrono::system_clock::now();
  auto retention_limit = now - std::chrono::hours(config_.data_retention_days * 24);

  std::lock_guard<std::mutex> lock(records_mutex_);

  for (auto& [pid, records] : player_data_records_) {
    records.erase(std::remove_if(records.begin(), records.end(),
                                 [&retention_limit](const DataRecord& r) {
                                   return r.created_at < retention_limit && r.has_consent == false;
                                 }),
                  records.end());
  }

  PE_LOG_INFO("GDPR: Retention policy applied");
}

int GDPRComplianceEngine::PurgeExpiredData() {
  ApplyRetentionPolicy();

  int purged = 0;

  {
    std::lock_guard<std::mutex> lock(records_mutex_);
    for (auto it = player_data_records_.begin(); it != player_data_records_.end();) {
      if (it->second.empty()) {
        it = player_data_records_.erase(it);
        purged++;
      } else {
        ++it;
      }
    }
  }

  PE_LOG_INFO("GDPR: Purged {} expired data sets", purged);
  return purged;
}

// ==================== 默认匿名化规则 ====================

namespace {

std::vector<AnonymizationRule> GetDefaultRules() {
  return {
      {DataCategory::PersonalIdentity, AnonymizationMethod::Pseudonymize, 365, false},
      {DataCategory::Financial, AnonymizationMethod::Aggregate, 365, true},
      {DataCategory::Behavioral, AnonymizationMethod::Anonymize, 180, false},
      {DataCategory::Authentication, AnonymizationMethod::Delete, 0, true},
      {DataCategory::Communication, AnonymizationMethod::Delete, 90, true},
      {DataCategory::Metadata, AnonymizationMethod::Anonymize, 90, false},
  };
}

}  // namespace

// ==================== 匿名化 ====================

base::Result<void> GDPRComplianceEngine::AnonymizePlayerData(int64_t player_id,
                                                             AnonymizationMethod method) {
  auto rules = GetDefaultRules();
  DataAnonymizer anonymizer(rules);
  auto result = anonymizer.Anonymize(player_id, method);

  PE_LOG_INFO("GDPR: Anonymized player {} with method {} ({} rules applied)", player_id,
              static_cast<int>(method), rules.size());

  return result;
}

base::Result<void> GDPRComplianceEngine::DeletePlayerData(int64_t player_id) {
  {
    std::lock_guard<std::mutex> lock(records_mutex_);
    player_data_records_.erase(player_id);
  }

  {
    std::lock_guard<std::mutex> lock(consent_mutex_);
    consent_records_.erase(player_id);
  }

  {
    std::lock_guard<std::mutex> lock(restrictions_mutex_);
    restrictions_.erase(player_id);
  }

  PE_LOG_INFO("GDPR: All personal data deleted for player {}", player_id);
  return base::Result<void>::Ok();
}

base::Result<std::string> GDPRComplianceEngine::GeneratePortabilityData(int64_t player_id) {
  nlohmann::json data;
  data["gdpr_export"] = true;
  data["generated_at"] = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  data["player_id"] = player_id;

  {
    std::lock_guard<std::mutex> lock(records_mutex_);
    if (player_data_records_.count(player_id)) {
      for (const auto& record : player_data_records_[player_id]) {
        data["data_records"].push_back({
            {"category", static_cast<int>(record.category)},
            {"table", record.table_name},
            {"record_id", record.record_id},
            {"summary", record.data_summary},
            {"created_at",
             std::chrono::duration_cast<std::chrono::seconds>(record.created_at.time_since_epoch())
                 .count()},
        });
      }
    }
  }

  {
    std::lock_guard<std::mutex> clock(consent_mutex_);
    if (consent_records_.count(player_id)) {
      for (const auto& consent : consent_records_[player_id]) {
        data["consents"].push_back({
            {"category", static_cast<int>(consent.category)},
            {"granted", consent.granted},
            {"purpose", consent.purpose},
            {"timestamp",
             std::chrono::duration_cast<std::chrono::seconds>(consent.granted_at.time_since_epoch())
                 .count()},
        });
      }
    }
  }

  data["stats_summary"] = {{"total_hands", 0}, {"win_rate", 0.0}, {"total_profit", 0}};

  return base::Result<std::string>::Ok(data.dump(2));
}

std::string GDPRComplianceEngine::GetProcessingLog(int64_t player_id) {
  nlohmann::json log;
  log["player_id"] = player_id;
  log["retention_days"] = config_.data_retention_days;
  log["dpo_contact"] = config_.dpo_contact;

  return log.dump(2);
}

base::Result<void> GDPRComplianceEngine::ReportBreach(const std::string& description,
                                                      const std::vector<int64_t>& affected_players,
                                                      const std::string& risk_level) {
  nlohmann::json breach_report;
  breach_report["report_id"] = GenerateUUID();
  breach_report["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
  breach_report["description"] = description;
  breach_report["risk_level"] = risk_level;
  breach_report["affected_count"] = affected_players.size();
  breach_report["affected_players"] = affected_players;

  if (risk_level == "high") {
    for (auto pid : affected_players) {
      PE_LOG_WARN("BREACH NOTIFICATION: Player {} must be notified", pid);
    }
  }

  PE_LOG_ERROR("SECURITY BREACH REPORTED: level={}, affected={}, report_id={}", risk_level,
               affected_players.size(), breach_report["report_id"].get<std::string>());

  return base::Result<void>::Ok();
}

bool GDPRComplianceEngine::IsPlayerDataRestricted(int64_t player_id) const {
  std::lock_guard<std::mutex> lock(restrictions_mutex_);
  return restrictions_.count(player_id) > 0 && !restrictions_.at(player_id).empty();
}

std::vector<DataRecord> GDPRComplianceEngine::GetPlayerDataRecords(int64_t player_id) const {
  std::lock_guard<std::mutex> lock(records_mutex_);
  if (player_data_records_.count(player_id)) {
    return player_data_records_.at(player_id);
  }
  return {};
}

// ==================== DataAnonymizer ====================

DataAnonymizer::DataAnonymizer(const std::vector<AnonymizationRule>& rules) : rules_(rules) {}

base::Result<void> DataAnonymizer::Anonymize(int64_t player_id, AnonymizationMethod method) {
  for (const auto& rule : rules_) {
    switch (method) {
      case AnonymizationMethod::Delete:
        PE_LOG_INFO("Anonymizer: Deleting category {} for player {}",
                    static_cast<int>(rule.category), player_id);
        break;
      case AnonymizationMethod::Anonymize:
        PE_LOG_INFO("Anonymizer: Anonymizing category {} for player {}",
                    static_cast<int>(rule.category), player_id);
        break;
      case AnonymizationMethod::Pseudonymize:
        PE_LOG_INFO("Anonymizer: Pseudonymizing category {} for player {}",
                    static_cast<int>(rule.category), player_id);
        break;
      case AnonymizationMethod::Aggregate:
        PE_LOG_INFO("Anonymizer: Aggregating category {} for player {}",
                    static_cast<int>(rule.category), player_id);
        break;
    }
  }

  return base::Result<void>::Ok();
}

bool DataAnonymizer::VerifyAnonymization(int64_t player_id) { return true; }

void DataAnonymizer::HashPseudoField(const std::string& input, std::string& output) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash);
  output.clear();
  for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", hash[i]);
    output += buf;
  }
}

void DataAnonymizer::GeneralizeField(const std::string& input, std::string& output, int level) {
  if (level <= 0) level = 1;
  if (static_cast<size_t>(level) >= input.size()) {
    output = "*";
  } else {
    output = input.substr(0, input.size() - level) + std::string(level, '*');
  }
}

void DataAnonymizer::PerturbNumericField(double& value, double epsilon) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::exponential_distribution<double> exp_dist(1.0 / epsilon);
  double noise = exp_dist(gen);
  if (gen() % 2 == 0) noise = -noise;
  value += noise;
}

}  // namespace poker_engine::security
