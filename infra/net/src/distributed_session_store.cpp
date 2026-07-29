#include "poker_engine/network/distributed_session_store.h"

#include <openssl/sha.h>

#include <iomanip>
#include <sstream>

namespace poker_engine::network {
namespace {

std::string Sha256Hex(const std::string& input) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash);
  std::ostringstream oss;
  for (unsigned char c : hash) {
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(c);
  }
  return oss.str();
}

}  // namespace

DistributedSessionStore::DistributedSessionStore(std::string instance_id)
    : instance_id_(std::move(instance_id)) {}

bool DistributedSessionStore::ConnectRedis(const std::string& redis_url) {
  RedisConfig cfg;
  if (!RedisClient::ParseUrl(redis_url, cfg)) return false;
  return redis_.Connect(cfg);
}

void DistributedSessionStore::Disconnect() { redis_.Disconnect(); }

bool DistributedSessionStore::IsRedisConnected() const { return redis_.IsConnected(); }

std::string DistributedSessionStore::TokenKey(const std::string& token) {
  return "poker:revoked:" + Sha256Hex(token);
}

std::string DistributedSessionStore::PlayerKey(int64_t player_id) {
  return "poker:player:" + std::to_string(player_id) + ":instance";
}

std::string DistributedSessionStore::InstanceKey(const std::string& instance_id) {
  return "poker:instance:" + instance_id;
}

bool DistributedSessionStore::IsRevoked(const std::string& token) const {
  {
    std::lock_guard<std::mutex> lock(local_mutex_);
    if (local_revoked_.count(token) > 0) return true;
  }
  if (!redis_.IsConnected()) return false;
  auto v = redis_.Get(TokenKey(token));
  return v.has_value();
}

void DistributedSessionStore::Revoke(const std::string& token, int ttl_seconds) {
  {
    std::lock_guard<std::mutex> lock(local_mutex_);
    local_revoked_.insert(token);
  }
  if (!redis_.IsConnected()) return;
  redis_.Set(TokenKey(token), "1", ttl_seconds > 0 ? ttl_seconds : 86400);
}

void DistributedSessionStore::BindPlayerInstance(int64_t player_id) {
  if (!redis_.IsConnected() || player_id <= 0) return;
  redis_.Set(PlayerKey(player_id), instance_id_, 3600);
}

std::optional<std::string> DistributedSessionStore::LookupPlayerInstance(int64_t player_id) const {
  if (!redis_.IsConnected() || player_id <= 0) return std::nullopt;
  return redis_.Get(PlayerKey(player_id));
}

void DistributedSessionStore::ClearPlayerInstance(int64_t player_id) {
  if (!redis_.IsConnected() || player_id <= 0) return;
  redis_.Del(PlayerKey(player_id));
}

bool DistributedSessionStore::Heartbeat(int ttl_seconds) {
  if (!redis_.IsConnected()) return false;
  return redis_.Set(InstanceKey(instance_id_), "alive", ttl_seconds);
}

}  // namespace poker_engine::network
