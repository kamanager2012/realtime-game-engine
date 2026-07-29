#pragma once

#include <cstdint>
#include <string>

#include "poker_engine/phase14/account_repository.h"
#include "poker_engine/phase14/chip_ledger.h"

namespace poker_engine::phase14 {

// Best-effort analytics mirror: SQLite remains source of truth.
class PostgresMirror {
 public:
  PostgresMirror();
  ~PostgresMirror();

  bool Connect(const std::string& connection_url);
  void Disconnect();
  bool IsConnected() const;
  bool IsEnabled() const { return enabled_; }

  bool MirrorAccount(const AccountData& account);
  bool MirrorWalletEvent(const WalletMirrorEvent& event);
  bool SyncAccountChips(int64_t player_id, int64_t chips);

 private:
  bool enabled_ = false;
#if defined(POKER_HAVE_LIBPQ)
  struct Impl;
  Impl* impl_ = nullptr;
#endif
};

}  // namespace poker_engine::phase14
