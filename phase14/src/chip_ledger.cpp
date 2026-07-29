#include "poker_engine/phase14/chip_ledger.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

#include "poker_engine/phase14/database.h"

namespace poker_engine::phase14 {
namespace {
}  // namespace

ChipLedger::ChipLedger(Database& db) : db_(db) {}

std::optional<int64_t> ChipLedger::GetBalance(int64_t player_id) const {
  auto rs = db_.Prepare("SELECT chips FROM accounts WHERE id = ?");
  StatementBinder(rs).Bind(1, player_id);
  if (!rs.Next()) return std::nullopt;
  return rs.GetRow().GetInt64(0);
}

LedgerResult ChipLedger::ApplyDelta(int64_t player_id, int64_t delta, const std::string& tx_type,
                                    const std::string& reference) {
  LedgerResult result;
  if (player_id <= 0) {
    result.error = "invalid_player_id";
    return result;
  }
  if (delta == 0) {
    result.error = "zero_amount";
    return result;
  }

  ScopedTransaction tx(db_);

  auto bal_rs = db_.Prepare("SELECT chips FROM accounts WHERE id = ?");
  StatementBinder(bal_rs).Bind(1, player_id);
  if (!bal_rs.Next()) {
    result.error = "account_not_found";
    return result;
  }
  int64_t current = bal_rs.GetRow().GetInt64(0);
  int64_t next = current + delta;
  if (next < 0) {
    result.error = "insufficient_chips";
    return result;
  }

  auto upd = db_.Prepare("UPDATE accounts SET chips = ? WHERE id = ? AND chips = ?");
  StatementBinder(upd).Bind(1, next).Bind(2, player_id).Bind(3, current);
  upd.Next();
  if (db_.RowsAffected() != 1) {
    result.error = "concurrent_update";
    return result;
  }

  auto ins = db_.Prepare(
      "INSERT INTO wallet_transactions (player_id, tx_type, amount, balance_after, reference) "
      "VALUES (?, ?, ?, ?, ?)");
  StatementBinder(ins)
      .Bind(1, player_id)
      .Bind(2, tx_type)
      .Bind(3, delta)
      .Bind(4, next)
      .Bind(5, reference);
  ins.Next();

  result.transaction_id = db_.LastInsertRowId();
  result.balance_after = next;
  result.success = true;
  tx.Commit();
  if (mirror_callback_) {
    WalletMirrorEvent ev;
    ev.player_id = player_id;
    ev.delta = delta;
    ev.balance_after = next;
    ev.transaction_id = result.transaction_id;
    ev.tx_type = tx_type;
    ev.reference = reference;
    mirror_callback_(ev);
  }
  return result;
}

LedgerResult ChipLedger::DebitBuyIn(int64_t player_id, int64_t amount,
                                    const std::string& reference) {
  if (amount <= 0) {
    return LedgerResult{false, "invalid_amount", 0, 0};
  }
  return ApplyDelta(player_id, -amount, "buy_in", reference);
}

LedgerResult ChipLedger::CreditCashOut(int64_t player_id, int64_t amount,
                                       const std::string& reference) {
  if (amount <= 0) {
    return LedgerResult{false, "invalid_amount", 0, 0};
  }
  return ApplyDelta(player_id, amount, "cash_out", reference);
}

LedgerResult ChipLedger::CreditRefund(int64_t player_id, int64_t amount,
                                      const std::string& reference) {
  if (amount <= 0) {
    return LedgerResult{false, "invalid_amount", 0, 0};
  }
  return ApplyDelta(player_id, amount, "refund", reference);
}

LedgerResult ChipLedger::CreditBonus(int64_t player_id, int64_t amount,
                                    const std::string& reference) {
  if (amount <= 0) {
    return LedgerResult{false, "invalid_amount", 0, 0};
  }
  return ApplyDelta(player_id, amount, "bonus", reference);
}

void ChipLedger::SetWalletMirrorCallback(WalletMirrorCallback cb) {
  mirror_callback_ = std::move(cb);
}

std::string ChipLedger::ReconcileReport::ToString() const {
  std::ostringstream oss;
  oss << "LedgerReconcile{all_balanced=" << (all_balanced ? "true" : "false")
      << " total_discrepancy=" << total_discrepancy << " tx_count=" << tx_count
      << " rows=" << rows.size() << "}";
  for (const auto& r : rows) {
    if (!r.balanced) {
      oss << "\n  MISMATCH player=" << r.player_id
          << " account=" << r.account_balance << " ledger=" << r.ledger_balance
          << " discrepancy=" << r.discrepancy;
    }
  }
  return oss.str();
}

ChipLedger::ReconcileReport ChipLedger::Reconcile() const {
  ReconcileReport report;

  // Total transaction count (single aggregate query).
  {
    auto rs = db_.Prepare("SELECT COUNT(*) FROM wallet_transactions");
    if (rs.Next()) report.tx_count = static_cast<size_t>(rs.GetRow().GetInt64(0));
  }

  // For every account, compare its live chip balance to the signed
  // sum of its wallet_transactions deltas (the immutable ledger).
  auto players = db_.Prepare("SELECT id, chips FROM accounts ORDER BY id");
  while (players.Next()) {
    auto row = players.GetRow();
    int64_t pid = row.GetInt64(0);
    int64_t account_balance = row.GetInt64(1);

    int64_t ledger_balance = 0;
    auto txs = db_.Prepare(
        "SELECT amount FROM wallet_transactions WHERE player_id = ? ORDER BY id");
    StatementBinder(txs).Bind(1, pid);
    while (txs.Next()) ledger_balance += txs.GetRow().GetInt64(0);

    ReconcileRow rr;
    rr.player_id = pid;
    rr.account_balance = account_balance;
    rr.ledger_balance = ledger_balance;
    rr.discrepancy = account_balance - ledger_balance;
    rr.balanced = (rr.discrepancy == 0);
    if (!rr.balanced) {
      report.all_balanced = false;
      report.total_discrepancy += std::abs(rr.discrepancy);
    }
    report.rows.push_back(rr);
  }
  return report;
}

bool ChipLedger::VerifyIntegrity() const {
  return Reconcile().all_balanced;
}

bool ChipLedger::ExportLedgerCSV(const std::string& path) const {
  std::ofstream out(path);
  if (!out) return false;
  out << "id,player_id,tx_type,amount,balance_after,reference,created_at\n";
  auto rs = db_.Prepare(
      "SELECT id, player_id, tx_type, amount, balance_after, reference, "
      "COALESCE(created_at, '') FROM wallet_transactions ORDER BY id");
  while (rs.Next()) {
    auto r = rs.GetRow();
    out << r.GetInt64(0) << ',' << r.GetInt64(1) << ',' << r.GetString(2) << ','
        << r.GetInt64(3) << ',' << r.GetInt64(4) << ",\""
        << r.GetString(5) << "\"," << r.GetString(6) << '\n';
  }
  return static_cast<bool>(out);
}

}  // namespace poker_engine::phase14
