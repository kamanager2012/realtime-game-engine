#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace poker_engine::phase14 {

class Database;

struct WalletMirrorEvent {
  int64_t player_id = 0;
  int64_t delta = 0;
  int64_t balance_after = 0;
  int64_t transaction_id = 0;
  std::string tx_type;
  std::string reference;
};

using WalletMirrorCallback = std::function<void(const WalletMirrorEvent&)>;

struct LedgerResult {
  bool success = false;
  std::string error;
  int64_t balance_after = 0;
  int64_t transaction_id = 0;
};

// Atomic chip movements backed by accounts.chips + wallet_transactions audit log.
class ChipLedger {
 public:
  explicit ChipLedger(Database& db);

  std::optional<int64_t> GetBalance(int64_t player_id) const;

  LedgerResult DebitBuyIn(int64_t player_id, int64_t amount, const std::string& reference);
  LedgerResult CreditCashOut(int64_t player_id, int64_t amount, const std::string& reference);
  LedgerResult CreditRefund(int64_t player_id, int64_t amount, const std::string& reference);
  // 每日奖励等系统发放（正 delta，tx_type=bonus）
  LedgerResult CreditBonus(int64_t player_id, int64_t amount, const std::string& reference);

  void SetWalletMirrorCallback(WalletMirrorCallback cb);

  // ----- Financial integrity -----

  // Per-player reconciliation: compares the running balance in
  // `accounts.chips` against the sum of all wallet_transactions
  // deltas. A balanced ledger has zero discrepancy.
  struct ReconcileRow {
    int64_t player_id = 0;
    int64_t account_balance = 0;
    int64_t ledger_balance = 0;  // sum of signed tx deltas
    int64_t discrepancy = 0;     // account - ledger
    bool balanced = true;
  };
  struct ReconcileReport {
    std::vector<ReconcileRow> rows;
    int64_t total_discrepancy = 0;
    bool all_balanced = true;
    size_t tx_count = 0;
    std::string ToString() const;
  };
  ReconcileReport Reconcile() const;

  // Convenience boolean: true iff every player's account balance
  // matches the signed sum of their transaction ledger (no tampering /
  // lost writes). Returns true even when there are zero transactions.
  bool VerifyIntegrity() const;

  // Export the immutable transaction ledger to CSV for audit / SIEM ingest.
  bool ExportLedgerCSV(const std::string& path) const;

 private:
  LedgerResult ApplyDelta(int64_t player_id, int64_t delta, const std::string& tx_type,
                          const std::string& reference);

  Database& db_;
  WalletMirrorCallback mirror_callback_;
};

}  // namespace poker_engine::phase14
