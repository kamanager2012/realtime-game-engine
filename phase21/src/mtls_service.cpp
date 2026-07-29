#include "poker_engine/security/mtls_service.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

#include "poker_engine/base/logging.h"

namespace poker_engine::security {

namespace {

std::chrono::system_clock::time_point ASN1ToTime(const ASN1_TIME* time) {
  struct tm tm{};
  const char* str = reinterpret_cast<const char*>(time->data);

  if (time->type == V_ASN1_UTCTIME) {
    strptime(str, "%y%m%d%H%M%SZ", &tm);
  } else {
    strptime(str, "%Y%m%d%H%M%SZ", &tm);
  }

  time_t t = timegm(&tm);
  return std::chrono::system_clock::from_time_t(t);
}

std::string SSLErrors() {
  std::string errors;
  unsigned long err;
  while ((err = ERR_get_error()) != 0) {
    if (!errors.empty()) errors += "; ";
    errors += ERR_error_string(err, nullptr);
  }
  return errors;
}

}  // namespace

// ==================== CRLManager ====================

CRLManager::CRLManager() = default;

base::Result<void> CRLManager::LoadCRL(const std::string& crl_url) {
#ifdef POKER_ENGINE_USE_VAULT
  PE_LOG_INFO("Loading CRL from: {}", crl_url);
  current_crl_url_ = crl_url;
  return base::Result<void>::Ok();
#else
  return base::Result<void>::Err(base::MakeErrorCode(base::Error::IoError));
#endif
}

base::Result<void> CRLManager::LoadCRLFromFile(const std::string& filepath) {
  FILE* fp = fopen(filepath.c_str(), "rb");
  if (!fp) {
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::IoError));
  }

  X509_CRL* crl = PEM_read_X509_CRL(fp, nullptr, nullptr, nullptr);
  fclose(fp);

  if (!crl) {
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::ParseError));
  }

  std::unique_ptr<X509_CRL, decltype(&X509_CRL_free)> crl_guard(crl, X509_CRL_free);

  STACK_OF(X509_REVOKED)* revoked = X509_CRL_get_REVOKED(crl);
  if (revoked) {
    std::lock_guard<std::mutex> lock(mutex_);
    revoked_list_.clear();

    for (int i = 0; i < sk_X509_REVOKED_num(revoked); ++i) {
      const X509_REVOKED* rev = sk_X509_REVOKED_value(revoked, i);

      const ASN1_INTEGER* serial = X509_REVOKED_get0_serialNumber(rev);
      BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
      if (bn) {
        char* hex = BN_bn2hex(bn);
        RevokedEntry entry;
        entry.serial_number = hex ? hex : "";
        OPENSSL_free(hex);
        BN_free(bn);

        const ASN1_TIME* rev_time = X509_REVOKED_get0_revocationDate(rev);
        if (rev_time) {
          entry.revocation_date = ASN1ToTime(rev_time);
        }

        revoked_list_.push_back(entry);
      }
    }
  }

  PE_LOG_INFO("CRL loaded: {} revoked certificates", revoked_list_.size());
  return base::Result<void>::Ok();
}

bool CRLManager::IsRevoked(const std::string& serial_number) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::any_of(
      revoked_list_.begin(), revoked_list_.end(),
      [&serial_number](const RevokedEntry& entry) { return entry.serial_number == serial_number; });
}

void CRLManager::SetRefreshInterval(std::chrono::seconds interval) { refresh_interval_ = interval; }

void CRLManager::RefreshLoop() {
  while (running_.load()) {
    std::this_thread::sleep_for(refresh_interval_);

    if (!running_.load()) break;

    if (!current_crl_url_.empty()) {
      auto result = LoadCRL(current_crl_url_);
      if (!result.IsOk()) {
        PE_LOG_WARN("CRL refresh failed: {}", result.Error().message());
      } else {
        PE_LOG_INFO("CRL refreshed successfully");
      }
    }
  }
}

// ==================== MTLSService ====================

MTLSService::MTLSService(const Config& config) : config_(config) {}

MTLSService::~MTLSService() {
  if (ssl_ctx_) {
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
  }

  EVP_cleanup();
  CRYPTO_cleanup_all_ex_data();
  ERR_free_strings();
}

base::Result<void> MTLSService::Initialize() {
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();

  const SSL_METHOD* method = TLS_server_method();
  ssl_ctx_ = SSL_CTX_new(method);

  if (!ssl_ctx_) {
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::IoError));
  }

  SSL_CTX_set_min_proto_version(ssl_ctx_,
                                config_.min_tls_version >= 5 ? TLS1_3_VERSION : TLS1_2_VERSION);

  if (SSL_CTX_use_certificate_file(ssl_ctx_, config_.server_cert_path.c_str(), SSL_FILETYPE_PEM) <=
      0) {
    ERR_clear_error();
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::IoError));
  }

  if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, config_.server_key_path.c_str(), SSL_FILETYPE_PEM) <=
      0) {
    ERR_clear_error();
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::IoError));
  }

  if (!SSL_CTX_check_private_key(ssl_ctx_)) {
    return base::Result<void>::Err(base::MakeErrorCode(base::Error::IoError));
  }

  if (!config_.ca_cert_path.empty()) {
    if (SSL_CTX_load_verify_locations(ssl_ctx_, config_.ca_cert_path.c_str(), nullptr) <= 0) {
      ERR_clear_error();
      return base::Result<void>::Err(base::MakeErrorCode(base::Error::IoError));
    }
  }

  if (config_.require_client_cert) {
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, VerifyCallback);
  } else {
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, VerifyCallback);
  }

  SSL_CTX_set_cipher_list(ssl_ctx_,
                          "ECDHE-ECDSA-AES128-GCM-SHA256:"
                          "ECDHE-RSA-AES128-GCM-SHA256:"
                          "ECDHE-ECDSA-AES256-GCM-SHA384:"
                          "ECDHE-RSA-AES256-GCM-SHA384:"
                          "ECDHE-ECDSA-CHACHA20-POLY1305:"
                          "ECDHE-RSA-CHACHA20-POLY1305");

  if (!config_.dh_params_path.empty()) {
    FILE* dh_file = fopen(config_.dh_params_path.c_str(), "r");
    if (dh_file) {
      DH* dh = PEM_read_DHparams(dh_file, nullptr, nullptr, nullptr);
      fclose(dh_file);
      if (dh) {
        SSL_CTX_set_tmp_dh(ssl_ctx_, dh);
        DH_free(dh);
      }
    }
  }

  if (config_.enable_ocsp_stapling) {
    SSL_CTX_set_tlsext_status_type(ssl_ctx_, TLSEXT_STATUSTYPE_ocsp);
  }

  PE_LOG_INFO("mTLS service initialized (TLS 1.{}+, client_cert={})",
              config_.min_tls_version >= 5 ? "3" : "2",
              config_.require_client_cert ? "required" : "optional");

  return base::Result<void>::Ok();
}

SSL_CTX* MTLSService::GetSSLContext() { return ssl_ctx_; }

ClientCertContext MTLSService::VerifyClientCertificate(SSL* ssl) {
  ClientCertContext ctx;

  X509* cert = SSL_get_peer_certificate(ssl);
  if (!cert) {
    ctx.verify_result = CertVerifyResult::UnknownCA;
    return ctx;
  }

  ctx.cert_info = ExtractCertInfo(cert);
  ctx.cert_fingerprint = ComputeFingerprint(cert);

  if (!ctx.cert_info.IsValid()) {
    ctx.verify_result = CertVerifyResult::Expired;
    X509_free(cert);
    return ctx;
  }

  if (IsBlacklisted(ctx.cert_fingerprint)) {
    ctx.verify_result = CertVerifyResult::Blacklisted;
    stats_.blacklisted_rejections++;
    X509_free(cert);
    return ctx;
  }

  std::string serial_str;
  {
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    if (bn) {
      char* hex = BN_bn2hex(bn);
      serial_str = hex ? hex : "";
      OPENSSL_free(hex);
      BN_free(bn);
    }

    if (crl_manager_.IsRevoked(serial_str)) {
      ctx.verify_result = CertVerifyResult::Revoked;
      stats_.revoked_cert_checks++;
      X509_free(cert);
      return ctx;
    }
  }

  long verify_result = SSL_get_verify_result(ssl);
  if (verify_result != X509_V_OK) {
    switch (verify_result) {
      case X509_V_ERR_CERT_HAS_EXPIRED:
      case X509_V_ERR_CERT_NOT_YET_VALID:
        ctx.verify_result = CertVerifyResult::Expired;
        break;
      case X509_V_ERR_CERT_REVOKED:
        ctx.verify_result = CertVerifyResult::Revoked;
        break;
      case X509_V_ERR_INVALID_CA:
      case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
        ctx.verify_result = CertVerifyResult::UnknownCA;
        break;
      default:
        ctx.verify_result = CertVerifyResult::InvalidSignature;
        break;
    }
  }

  stats_.total_connections++;
  if (ctx.verify_result == CertVerifyResult::OK) {
    stats_.verified_connections++;
  } else {
    stats_.failed_verifications++;
    stats_.failure_reasons[ctx.verify_result]++;
  }

  X509_free(cert);
  ctx.connected_at = std::chrono::steady_clock::now();

  return ctx;
}

ClientCertContext MTLSService::VerifyClientCertificate(X509* cert) {
  ClientCertContext ctx;

  if (!cert) {
    ctx.verify_result = CertVerifyResult::UnknownCA;
    return ctx;
  }

  ctx.cert_info = ExtractCertInfo(cert);
  ctx.cert_fingerprint = ComputeFingerprint(cert);

  if (!ctx.cert_info.IsValid()) {
    ctx.verify_result = CertVerifyResult::Expired;
    return ctx;
  }

  if (IsBlacklisted(ctx.cert_fingerprint)) {
    ctx.verify_result = CertVerifyResult::Blacklisted;
    stats_.blacklisted_rejections++;
    return ctx;
  }

  stats_.total_connections++;
  stats_.verified_connections++;
  ctx.connected_at = std::chrono::steady_clock::now();

  return ctx;
}

int MTLSService::VerifyCallback(int preverify_ok, X509_STORE_CTX* ctx) {
  if (!preverify_ok) {
    int err = X509_STORE_CTX_get_error(ctx);
    PE_LOG_WARN("Certificate verification failed: {} (error={})",
                X509_verify_cert_error_string(err), err);
  }
  return preverify_ok;
}

base::Result<std::pair<std::string, std::string>> MTLSService::IssueClientCertificate(
    int64_t player_id, const std::string& cn) {
  if (!ssl_ctx_) {
    return base::Result<std::pair<std::string, std::string>>::Err(
        base::MakeErrorCode(base::Error::NotConnected));
  }

  X509* x509 = X509_new();
  EVP_PKEY* pkey = EVP_RSA_gen(2048);

  if (!x509 || !pkey) {
    X509_free(x509);
    EVP_PKEY_free(pkey);
    return base::Result<std::pair<std::string, std::string>>::Err(
        base::MakeErrorCode(base::Error::InternalError));
  }

  std::unique_ptr<X509, decltype(&X509_free)> x509_guard(x509, X509_free);
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey_guard(pkey, EVP_PKEY_free);

  ASN1_INTEGER_set(X509_get_serialNumber(x509), static_cast<long>(player_id));

  X509_gmtime_adj(X509_get_notBefore(x509), 0);
  X509_gmtime_adj(X509_get_notAfter(x509), config_.max_cert_age_days * 24 * 3600L);

  X509_NAME* name = X509_get_subject_name(x509);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);

  X509_NAME* issuer = X509_get_issuer_name(x509);
  X509_NAME_add_entry_by_txt(issuer, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>("PokerEngineCA"), -1, -1, 0);

  X509_set_pubkey(x509, pkey);

  X509V3_CTX ctx;
  X509V3_set_ctx_nodb(&ctx);
  X509V3_set_ctx(&ctx, x509, x509, nullptr, nullptr, 0);

  X509_EXTENSION* ext =
      X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage, "digitalSignature,keyEncipherment");
  if (ext) {
    X509_add_ext(x509, ext, -1);
    X509_EXTENSION_free(ext);
  }

  ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_ext_key_usage, "clientAuth");
  if (ext) {
    X509_add_ext(x509, ext, -1);
    X509_EXTENSION_free(ext);
  }

  if (X509_sign(x509, pkey, EVP_sha256()) == 0) {
    return base::Result<std::pair<std::string, std::string>>::Err(
        base::MakeErrorCode(base::Error::InternalError));
  }

  BIO* cert_bio = BIO_new(BIO_s_mem());
  BIO* key_bio = BIO_new(BIO_s_mem());

  PEM_write_bio_X509(cert_bio, x509);
  PEM_write_bio_PrivateKey(key_bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);

  BUF_MEM* cert_mem;
  BUF_MEM* key_mem;
  BIO_get_mem_ptr(cert_bio, &cert_mem);
  BIO_get_mem_ptr(key_bio, &key_mem);

  std::string cert_pem(cert_mem->data, cert_mem->length);
  std::string key_pem(key_mem->data, key_mem->length);

  BIO_free(cert_bio);
  BIO_free(key_bio);

  PE_LOG_INFO("Issued client cert for player {} (CN={})", player_id, cn);

  return base::Result<std::pair<std::string, std::string>>::Ok({cert_pem, key_pem});
}

base::Result<void> MTLSService::RevokeCertificate(const std::string& serial_number,
                                                  const std::string& reason) {
  AddToBlacklist(serial_number);

  PE_LOG_INFO("Certificate revoked: {} (reason: {})", serial_number, reason);
  return base::Result<void>::Ok();
}

void MTLSService::AddToBlacklist(const std::string& cert_fingerprint) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  blacklist_.insert(cert_fingerprint);
}

bool MTLSService::IsBlacklisted(const std::string& cert_fingerprint) const {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  return blacklist_.count(cert_fingerprint) > 0;
}

void MTLSService::RemoveFromBlacklist(const std::string& cert_fingerprint) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  blacklist_.erase(cert_fingerprint);
}

typename MTLSService::ServiceStats MTLSService::GetStats() const {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  return stats_;
}

CertificateInfo MTLSService::ExtractCertInfo(X509* cert) const {
  CertificateInfo info;

  X509_NAME* subject = X509_get_subject_name(cert);
  int cn_idx = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);
  if (cn_idx >= 0) {
    X509_NAME_ENTRY* cn_entry = X509_NAME_get_entry(subject, cn_idx);
    ASN1_STRING* cn_str = X509_NAME_ENTRY_get_data(cn_entry);
    info.subject_cn = reinterpret_cast<const char*>(ASN1_STRING_get0_data(cn_str));
  }

  X509_NAME* issuer = X509_get_issuer_name(cert);
  int issuer_cn_idx = X509_NAME_get_index_by_NID(issuer, NID_commonName, -1);
  if (issuer_cn_idx >= 0) {
    X509_NAME_ENTRY* ie = X509_NAME_get_entry(issuer, issuer_cn_idx);
    ASN1_STRING* iss_str = X509_NAME_ENTRY_get_data(ie);
    info.issuer_cn = reinterpret_cast<const char*>(ASN1_STRING_get0_data(iss_str));
  }

  ASN1_INTEGER* serial = X509_get_serialNumber(cert);
  BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
  if (bn) {
    char* hex = BN_bn2hex(bn);
    info.serial_number = hex ? hex : "";
    OPENSSL_free(hex);
    BN_free(bn);
  }

  info.not_before = ASN1ToTime(X509_get_notBefore(cert));
  info.not_after = ASN1ToTime(X509_get_notAfter(cert));

  STACK_OF(GENERAL_NAME)* san_names = static_cast<STACK_OF(GENERAL_NAME)*>(
      X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));

  if (san_names) {
    for (int i = 0; i < sk_GENERAL_NAME_num(san_names); ++i) {
      GENERAL_NAME* gen_name = sk_GENERAL_NAME_value(san_names, i);
      if (gen_name->type == GEN_DNS) {
        ASN1_STRING* dns = gen_name->d.dNSName;
        info.san_dns.push_back(reinterpret_cast<const char*>(ASN1_STRING_get0_data(dns)));
      }
    }
    sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
  }

  return info;
}

std::string MTLSService::ComputeFingerprint(X509* cert) const {
  unsigned char md[SHA256_DIGEST_LENGTH];

  unsigned char* der = nullptr;
  int len = i2d_X509(cert, &der);
  SHA256(der, len, md);
  OPENSSL_free(der);

  std::ostringstream oss;
  for (unsigned int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(md[i]);
  }
  return oss.str();
}

std::string MTLSService::VerifyResultToString(CertVerifyResult result) {
  switch (result) {
    case CertVerifyResult::OK:
      return "OK";
    case CertVerifyResult::Expired:
      return "Certificate expired";
    case CertVerifyResult::Revoked:
      return "Certificate revoked";
    case CertVerifyResult::InvalidSignature:
      return "Invalid signature";
    case CertVerifyResult::UnknownCA:
      return "Unknown CA";
    case CertVerifyResult::CNMismatch:
      return "CN mismatch";
    case CertVerifyResult::Blacklisted:
      return "Certificate blacklisted";
  }
  return "Unknown error";
}

}  // namespace poker_engine::security
