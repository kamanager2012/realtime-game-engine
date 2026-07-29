#include "poker_engine/security/vault_manager.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "poker_engine/base/logging.h"

#ifdef POKER_ENGINE_USE_VAULT
#include <curl/curl.h>
#endif

namespace poker_engine::security {

namespace {

static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const unsigned char* data, size_t len) {
  std::string ret;
  int i = 0, j = 0;
  unsigned char char_array_3[3], char_array_4[4];

  while (len--) {
    char_array_3[i++] = *(data++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;
      for (i = 0; i < 4; i++) ret += base64_chars[char_array_4[i]];
      i = 0;
    }
  }
  if (i) {
    for (j = i; j < 3; j++) char_array_3[j] = '\0';
    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
    for (j = 0; j < i + 1; j++) ret += base64_chars[char_array_4[j]];
  }
  return ret;
}

std::string Base64Decode(const std::string& encoded) {
  if (encoded.empty()) return "";

  size_t in_len = encoded.size();
  int i = 0, j = 0, in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::string ret;

  while (in_len-- && encoded[in_] != '=') {
    char_array_4[i++] = encoded[in_];
    in_++;
    if (i == 4) {
      for (i = 0; i < 4; i++)
        char_array_4[i] = static_cast<unsigned char>(base64_chars.find(char_array_4[i]));
      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
      for (i = 0; i < 3; i++) ret += char_array_3[i];
      i = 0;
    }
  }
  if (i) {
    for (j = i; j < 4; j++) char_array_4[j] = 0;
    for (j = 0; j < 4; j++)
      char_array_4[j] = static_cast<unsigned char>(base64_chars.find(char_array_4[j]));
    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
    for (j = 0; j < i - 1; j++) ret += char_array_3[j];
  }
  return ret;
}

#ifdef POKER_ENGINE_USE_VAULT
struct CurlDeleter {
  void operator()(CURL* c) const { curl_easy_cleanup(c); }
};
using CurlHandle = std::unique_ptr<CURL, CurlDeleter>;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
  size_t total = size * nmemb;
  output->append(static_cast<char*>(contents), total);
  return total;
}

base::Result<std::string> HttpGet(const std::string& url, const std::string& token = "") {
  CurlHandle curl(curl_easy_init());
  if (!curl) return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::NetworkError));

  std::string response;
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, ("X-Vault-Token: " + token).c_str());

  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 5L);
  curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);

  CURLcode res = curl_easy_perform(curl.get());
  curl_slist_free_all(headers);

  if (res != CURLE_OK) {
    return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::NetworkError));
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code != 200) {
    return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::PermissionDenied));
  }

  try {
    auto json = nlohmann::json::parse(response);
    return base::Result<std::string>::Ok(json["data"]["data"].dump());
  } catch (...) {
    return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::ParseError));
  }
}
#endif

}  // namespace

// ==================== K8sSecretClient ====================

K8sSecretClient::K8sSecretClient(const std::string& namespace_name) : namespace_(namespace_name) {}

std::string K8sSecretClient::ReadServiceAccountToken() const {
  const std::string token_path = "/var/run/secrets/kubernetes.io/serviceaccount/token";
  std::ifstream ifs(token_path);
  if (!ifs) return "";
  return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
}

std::string K8sSecretClient::Base64Decode(const std::string& encoded) const {
  return ::poker_engine::security::Base64Decode(encoded);
}

std::string K8sSecretClient::Base64Encode(const std::string& data) const {
  return ::poker_engine::security::Base64Encode(reinterpret_cast<const unsigned char*>(data.data()),
                                                data.size());
}

base::Result<void> K8sSecretClient::Authenticate() {
  api_token_ = ReadServiceAccountToken();
  if (api_token_.empty()) {
    PE_LOG_WARN("K8s: no service account token available");
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::AuthenticationFailed));
  }
  return base::Result<void>::Ok();
}

base::Result<std::string> K8sSecretClient::GetSecret(const std::string& name,
                                                     const std::string& key) {
#ifdef POKER_ENGINE_USE_VAULT
  std::string url = api_endpoint_ + "/api/v1/namespaces/" + namespace_ + "/secrets/" + name;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, ("Authorization: Bearer " + api_token_).c_str());

  CurlHandle curl(curl_easy_init());
  std::string response;

  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 5L);

  CURLcode res = curl_easy_perform(curl.get());
  curl_slist_free_all(headers);

  if (res != CURLE_OK) {
    return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::NetworkError));
  }

  auto json = nlohmann::json::parse(response);
  auto& data = json["data"]["data"];
  auto it = data.find(key);
  if (it == data.end()) {
    return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::NotFound));
  }

  return base::Result<std::string>::Ok(Base64Decode(it.value().get<std::string>()));
#else
  std::string env_key = namespace_ + "_" + name + "_" + key;
  const char* val = std::getenv(env_key.c_str());
  if (!val) return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::NotFound));
  return base::Result<std::string>::Ok(val);
#endif
}

base::Result<void> K8sSecretClient::CreateSecret(
    const std::string& name, const std::unordered_map<std::string, std::string>& data) {
#ifdef POKER_ENGINE_USE_VAULT
  nlohmann::json payload;
  payload["apiVersion"] = "v1";
  payload["kind"] = "Secret";
  payload["metadata"]["name"] = name;
  payload["metadata"]["namespace"] = namespace_;

  for (auto& [k, v] : data) {
    payload["data"][k] = Base64Encode(v);
  }

  CurlHandle curl(curl_easy_init());
  std::string url = api_endpoint_ + "/api/v1/namespaces/" + namespace_ + "/secrets";

  std::string response;
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, ("Authorization: Bearer " + api_token_).c_str());

  std::string body = payload.dump();
  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "POST");
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

  CURLcode res = curl_easy_perform(curl.get());
  curl_slist_free_all(headers);

  if (res != CURLE_OK) {
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::IoError));
  }
  return base::Result<void>::Ok();
#else
  return base::Result<void>::Err(base::MakeErrorCode(base::Error::NetworkError));
#endif
}

// ==================== VaultManager ====================

VaultManager::VaultManager(const Config& config) : config_(config) {}

VaultManager::~VaultManager() { StopLeaseRenewal(); }

base::Result<void> VaultManager::Initialize() {
  switch (config_.backend) {
    case SecretBackend::Vault: {
#ifdef POKER_ENGINE_USE_VAULT
      vault_client_ = std::make_unique<VaultClient>();
      auto result = vault_client_->Connect(config_.vault_address, config_.vault_token);
      if (!result.IsOk()) {
        PE_LOG_ERROR("Vault connection failed: {}", result.Error().message());
        return result;
      }
      connected_ = true;
      StartLeaseRenewal();
      PE_LOG_INFO("Vault connected at {}", config_.vault_address);
      return base::Result<void>::Ok();
#else
      return base::Result<void>::Err(base::MakeErrorCode(base::Error::IoError));
#endif
    }
    case SecretBackend::K8sSecret: {
      k8s_client_ = std::make_unique<K8sSecretClient>(config_.k8s_namespace);
      auto result = k8s_client_->Authenticate();
      if (!result.IsOk()) {
        PE_LOG_ERROR("K8s authentication failed");
        return result;
      }
      connected_ = true;
      PE_LOG_INFO("K8s Secrets client authenticated");
      return base::Result<void>::Ok();
    }
    case SecretBackend::EnvVar: {
      connected_ = true;
      PE_LOG_INFO("Using environment variables for secrets");
      return base::Result<void>::Ok();
    }
    case SecretBackend::File: {
      connected_ = true;
      PE_LOG_INFO("Using file-based secrets (dev only)");
      return base::Result<void>::Ok();
    }
  }
  return base::Result<void>::Err(base::MakeErrorCode(base::Error::InvalidArgument));
}

base::Result<std::string> VaultManager::GetSecret(const std::string& key) {
  // 检查缓存
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = secret_cache_.find(key);
    if (it != secret_cache_.end()) {
      if (std::chrono::steady_clock::now() - it->second.cached_at < it->second.ttl) {
        return base::Result<std::string>::Ok(it->second.value);
      }
      secret_cache_.erase(it);
    }
  }

  std::string value;
  switch (config_.backend) {
    case SecretBackend::Vault:
      value = GetFromVault(key).UnwrapOr("");
      break;
    case SecretBackend::K8sSecret:
      value = GetFromK8s(key).UnwrapOr("");
      break;
    case SecretBackend::EnvVar:
      value = GetFromEnv(key).UnwrapOr("");
      break;
    case SecretBackend::File: {
      std::ifstream ifs(".secrets/" + key);
      if (ifs)
        value = std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
      break;
    }
  }

  if (value.empty()) {
    return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::NotFound));
  }

  // 更新缓存
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    secret_cache_[key] = {value, std::chrono::steady_clock::now(), std::chrono::seconds(300)};
  }

  return base::Result<std::string>::Ok(value);
}

base::Result<std::string> VaultManager::GetCachedSecret(const std::string& key,
                                                        std::chrono::seconds cache_ttl) {
  return GetSecret(key);
}

base::Result<std::pair<std::string, std::string>> VaultManager::GetDatabaseCredential(
    const std::string& role) {
#ifdef POKER_ENGINE_USE_VAULT
  if (config_.backend == SecretBackend::Vault && vault_client_) {
    return vault_client_->GetDatabaseCredential(role);
  }
#endif
  auto user = GetSecret(role + "_username");
  auto pass = GetSecret(role + "_password");
  if (!user.IsOk() || !pass.IsOk()) {
    return base::Result<std::pair<std::string, std::string>>::Err(
        base::MakeErrorCode(base::Error::NotFound));
  }
  return base::Result<std::pair<std::string, std::string>>::Ok({user.Unwrap(), pass.Unwrap()});
}

base::Result<std::string> VaultManager::GetJWTSigningKey() { return GetSecret("jwt_signing_key"); }

base::Result<std::pair<std::string, std::string>> VaultManager::GetTLSCertificate(
    const std::string& domain) {
  auto cert = GetSecret("tls/" + domain + "/cert");
  auto key = GetSecret("tls/" + domain + "/key");
  if (!cert.IsOk() || !key.IsOk()) {
    return base::Result<std::pair<std::string, std::string>>::Err(
        base::MakeErrorCode(base::Error::NotFound));
  }
  return base::Result<std::pair<std::string, std::string>>::Ok({cert.Unwrap(), key.Unwrap()});
}

base::Result<void> VaultManager::RotateKey(const std::string& key, const std::string& new_value) {
  switch (config_.backend) {
    case SecretBackend::Vault: {
      auto result = GetFromVault("secret/data/" + key);
      if (!result.IsOk()) {
        return base::Result<void>::Err(result.Error());
      }
      return base::Result<void>::Ok();
    }
    case SecretBackend::K8sSecret: {
      if (!k8s_client_) break;
      std::unordered_map<std::string, std::string> data;
      data[key] = new_value;
      auto pos = key.rfind('/');
      std::string secret_name = (pos != std::string::npos) ? key.substr(0, pos) : key;
      return k8s_client_->CreateSecret(secret_name, data);
    }
    default:
      break;
  }
  return base::Result<void>::Err(base::MakeErrorCode(base::Error::PermissionDenied));
}

base::Result<void> VaultManager::RenewLease(const std::string& lease_id) {
#ifdef POKER_ENGINE_USE_VAULT
  if (vault_client_) {
    return vault_client_->RenewLease(lease_id);
  }
#endif
  return base::Result<void>::Err(base::MakeErrorCode(base::Error::OperationNotSupported));
}

void VaultManager::StartLeaseRenewal() {
  renewal_running_ = true;
  renewal_thread_ = std::thread([this]() {
    while (renewal_running_.load()) {
      std::this_thread::sleep_for(config_.lease_renewal_interval);

      if (!renewal_running_.load()) break;

      std::lock_guard<std::mutex> lock(cache_mutex_);
      for (auto& [key, cached] : secret_cache_) {
        if (!cached.lease_id.empty()) {
          auto result = RenewLease(cached.lease_id);
          if (!result.IsOk()) {
            PE_LOG_WARN("Lease renewal failed for key: {}", key);
            secret_cache_.erase(key);
          }
        }
      }
    }
  });
}

void VaultManager::StopLeaseRenewal() {
  renewal_running_ = false;
  if (renewal_thread_.joinable()) {
    renewal_thread_.join();
  }
}

base::Result<std::string> VaultManager::GetFromVault(const std::string& key) {
#ifdef POKER_ENGINE_USE_VAULT
  if (!vault_client_) {
    return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::NotConnected));
  }
  return vault_client_->ReadSecret("secret/data/" + key);
#else
  return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::IoError));
#endif
}

base::Result<std::string> VaultManager::GetFromK8s(const std::string& key) {
  if (!k8s_client_) {
    return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::NotConnected));
  }
  size_t pos = key.find('/');
  if (pos == std::string::npos) {
    return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::InvalidArgument));
  }
  return k8s_client_->GetSecret(key.substr(0, pos), key.substr(pos + 1));
}

base::Result<std::string> VaultManager::GetFromEnv(const std::string& key) {
  std::string env_name = config_.fallback_prefix + key;
  std::replace(env_name.begin(), env_name.end(), '/', '_');

  const char* val = std::getenv(env_name.c_str());
  if (!val) return base::Result<std::string>::Err(base::MakeErrorCode(base::Error::NotFound));
  return base::Result<std::string>::Ok(val);
}

}  // namespace poker_engine::security
