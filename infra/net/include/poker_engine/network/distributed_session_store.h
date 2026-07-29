#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>

#include "poker_engine/network/auth_service.h"
#include "poker_engine/network/redis_client.h"

namespace poker_engine::network {

// Shared token revocation + player/instance routing for multi-node deployments.
class DistributedSessionStore : public TokenRevocationBackend {
 public:
  explicit DistributedSessionStore(std::string instance_id);

  bool ConnectRedis(const std::string& redis_url);
  void Disconnect();
  bool IsRedisConnected() const;

  // TokenRevocationBackend
  bool IsRevoked(const std::string& token) const override;
  void Revoke(const std::string& token, int ttl_seconds) override;

  void BindPlayerInstance(int64_t player_id);
  std::optional<std::string> LookupPlayerInstance(int64_t player_id) const;
  void ClearPlayerInstance(int64_t player_id);

  bool Heartbeat(int ttl_seconds = 30);
  std::string instance_id() const { return instance_id_; }

 private:
  static std::string TokenKey(const std::string& token);
  static std::string PlayerKey(int64_t player_id);
  static std::string InstanceKey(const std::string& instance_id);

  std::string instance_id_;
  mutable std::mutex local_mutex_;
  std::unordered_set<std::string> local_revoked_;
  mutable RedisClient redis_;
};

using DistributedSessionStorePtr = std::shared_ptr<DistributedSessionStore>;

}  // namespace poker_engine::network
