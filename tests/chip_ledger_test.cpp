#include <gtest/gtest.h>

#include <fstream>
#include <filesystem>

#include "poker_engine/phase14/account_repository.h"
#include "poker_engine/phase14/chip_ledger.h"
#include "poker_engine/phase14/database.h"
#include "poker_engine/phase14/migration.h"

using namespace poker_engine::phase14;

class ChipLedgerTest : public ::testing::Test {
 protected:
  Database db_;
  AccountRepository* accounts = nullptr;
  ChipLedger* ledger = nullptr;

  void SetUp() override {
    path_ = "/tmp/test_chip_ledger.db";
    std::filesystem::remove(path_);
    ASSERT_TRUE(db_.Open(path_));
    MigrationManager mgr(db_);
    mgr.AddMigration(6, "accounts", R"(
      CREATE TABLE IF NOT EXISTS accounts (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT NOT NULL UNIQUE,
        display_name TEXT,
        password_hash TEXT NOT NULL,
        chips INTEGER DEFAULT 1000,
        elo_rating INTEGER DEFAULT 1500,
        total_profit INTEGER DEFAULT 0,
        hands_played INTEGER DEFAULT 0,
        avatar_url TEXT DEFAULT '',
        created_at TEXT DEFAULT (datetime('now')),
        last_login TEXT DEFAULT (datetime('now'))
      );
    )");
    mgr.AddMigration(7, "wallet", R"(
      CREATE TABLE IF NOT EXISTS wallet_transactions (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        player_id INTEGER NOT NULL,
        tx_type TEXT NOT NULL,
        amount INTEGER NOT NULL,
        balance_after INTEGER NOT NULL,
        reference TEXT DEFAULT '',
        note TEXT DEFAULT '',
        created_at TEXT DEFAULT (datetime('now'))
      );
    )");
    mgr.MigrateToLatest();
    accounts = new AccountRepository(db_);
    ledger = new ChipLedger(db_);
    AccountData a;
    a.id = 42;
    a.username = "alice";
    a.display_name = "Alice";
    a.password_hash = "salt$hash";
    a.chips = 1000;
    ASSERT_TRUE(accounts->SaveAccount(a));
  }

  void TearDown() override {
    delete ledger;
    delete accounts;
    db_.Close();
    std::filesystem::remove(path_);
  }

  std::string path_;
};

TEST_F(ChipLedgerTest, DebitBuyInReducesBalance) {
  auto r = ledger->DebitBuyIn(42, 200, "main");
  ASSERT_TRUE(r.success);
  EXPECT_EQ(r.balance_after, 800);
  EXPECT_EQ(ledger->GetBalance(42), 800);
}

TEST_F(ChipLedgerTest, InsufficientChipsRejected) {
  auto r = ledger->DebitBuyIn(42, 2000, "main");
  EXPECT_FALSE(r.success);
  EXPECT_EQ(r.error, "insufficient_chips");
  EXPECT_EQ(ledger->GetBalance(42), 1000);
}

TEST_F(ChipLedgerTest, CashOutAndRefundRestoreBalance) {
  ASSERT_TRUE(ledger->DebitBuyIn(42, 300, "main").success);
  ASSERT_TRUE(ledger->CreditCashOut(42, 250, "main").success);
  EXPECT_EQ(ledger->GetBalance(42), 950);
  ASSERT_TRUE(ledger->CreditRefund(42, 300, "main:rollback").success);
  EXPECT_EQ(ledger->GetBalance(42), 1250);
}

TEST_F(ChipLedgerTest, LedgerWritesAuditRows) {
  ASSERT_TRUE(ledger->DebitBuyIn(42, 100, "table_1").success);
  auto rs = db_.Query("SELECT COUNT(*) FROM wallet_transactions WHERE player_id = 42");
  ASSERT_TRUE(rs.Next());
  // grant (account creation) + buy-in
  EXPECT_EQ(rs.GetRow().GetInt(0), 2);
}

// After a balanced sequence of debits/credits the append-only ledger
// must reconcile exactly with the live account balance.
TEST_F(ChipLedgerTest, ReconcileBalanced) {
  ASSERT_TRUE(ledger->DebitBuyIn(42, 500, "main").success);    // 1000 -> 500
  ASSERT_TRUE(ledger->CreditCashOut(42, 200, "main").success);  // 500 -> 700
  ASSERT_TRUE(ledger->CreditRefund(42, 100, "main:rb").success);  // 700 -> 800
  EXPECT_EQ(ledger->GetBalance(42), 800);

  auto report = ledger->Reconcile();
  EXPECT_TRUE(report.all_balanced) << report.ToString();
  EXPECT_EQ(report.total_discrepancy, 0);
  // grant (account creation) + 3 settlement transactions
  EXPECT_EQ(report.tx_count, 4u);
  EXPECT_TRUE(ledger->VerifyIntegrity());
}

// The ledger is the source of truth: if the account balance is
// tampered with (e.g. a lost/partial write), reconciliation must
// surface the discrepancy rather than silently pass.
TEST_F(ChipLedgerTest, ReconcileDetectsTampering) {
  ASSERT_TRUE(ledger->DebitBuyIn(42, 500, "main").success);  // 1000 -> 500
  // Direct, ledger-bypassing update (simulates corruption / drift).
  db_.Execute("UPDATE accounts SET chips = 123 WHERE id = 42");
  EXPECT_EQ(ledger->GetBalance(42), 123);

  auto report = ledger->Reconcile();
  EXPECT_FALSE(report.all_balanced);
  EXPECT_FALSE(ledger->VerifyIntegrity());
  EXPECT_NE(report.total_discrepancy, 0);
}

// The immutable transaction log must be exportable for SIEM / audit.
TEST_F(ChipLedgerTest, ExportLedgerCSV) {
  ASSERT_TRUE(ledger->DebitBuyIn(42, 500, "main").success);
  std::string path = "/tmp/test_ledger_export.csv";
  std::filesystem::remove(path);
  ASSERT_TRUE(ledger->ExportLedgerCSV(path));
  auto rs = db_.Query("SELECT COUNT(*) FROM wallet_transactions");
  ASSERT_TRUE(rs.Next());
  int64_t n = rs.GetRow().GetInt64(0);
  std::ifstream in(path);
  int lines = 0;
  std::string line;
  while (std::getline(in, line)) ++lines;
  // header + n data rows
  EXPECT_EQ(lines, n + 1);
  std::filesystem::remove(path);
}
