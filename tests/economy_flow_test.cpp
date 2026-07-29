#include <gtest/gtest.h>

#include <filesystem>

#include "poker_engine/network/game_server.h"
#include "poker_engine/phase14/account_repository.h"
#include "poker_engine/phase14/chip_ledger.h"
#include "poker_engine/phase14/database.h"
#include "poker_engine/phase14/migration.h"
#include "poker_engine/tournament/tournament_server.h"

using namespace poker_engine::network;
using namespace poker_engine::phase14;
using namespace poker_engine::tournament;

namespace {

void SetupWalletSchema(Database& db, AccountRepository** accounts, ChipLedger** ledger) {
  MigrationManager mgr(db);
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
  *accounts = new AccountRepository(db);
  *ledger = new ChipLedger(db);
}

AccountData MakeAccount(int64_t id, int64_t chips) {
  AccountData a;
  a.id = id;
  a.username = "player_" + std::to_string(id);
  a.display_name = "Player";
  a.password_hash = "salt$hash";
  a.chips = chips;
  return a;
}

}  // namespace

class EconomyFlowTest : public ::testing::Test {
 protected:
  Database db_;
  AccountRepository* accounts = nullptr;
  ChipLedger* ledger = nullptr;
  std::string path_ = "/tmp/test_economy_flow.db";

  void SetUp() override {
    std::filesystem::remove(path_);
    ASSERT_TRUE(db_.Open(path_));
    SetupWalletSchema(db_, &accounts, &ledger);
    ASSERT_TRUE(accounts->SaveAccount(MakeAccount(7, 1000)));
  }

  void TearDown() override {
    delete ledger;
    delete accounts;
    db_.Close();
    std::filesystem::remove(path_);
  }
};

TEST_F(EconomyFlowTest, TableJoinBuyInAndCashOutRoundTrip) {
  GameServer game;
  const std::string table_id = "eco_table";
  ASSERT_FALSE(game.CreateTable(table_id, 6, 1, 2).empty());

  const int64_t player_id = 7;
  const int64_t buy_in = 200;

  auto debit = ledger->DebitBuyIn(player_id, buy_in, table_id);
  ASSERT_TRUE(debit.success);
  EXPECT_EQ(debit.balance_after, 800);

  ASSERT_TRUE(game.JoinTable(static_cast<int32_t>(player_id), table_id, "Alice", -1, buy_in, ""));

  const double stack = game.GetPlayerStack(table_id, static_cast<int32_t>(player_id));
  EXPECT_DOUBLE_EQ(stack, static_cast<double>(buy_in));

  ASSERT_TRUE(game.LeaveTable(static_cast<int32_t>(player_id), table_id));

  auto credit = ledger->CreditCashOut(player_id, static_cast<int64_t>(stack), table_id);
  ASSERT_TRUE(credit.success);
  EXPECT_EQ(credit.balance_after, 1000);

  auto rs = db_.Query(
      "SELECT COUNT(*) FROM wallet_transactions WHERE player_id = 7 AND tx_type IN ('buy_in','cash_out')");
  ASSERT_TRUE(rs.Next());
  EXPECT_EQ(rs.GetRow().GetInt(0), 2);
}

TEST_F(EconomyFlowTest, FailedJoinRefundsBuyIn) {
  GameServer game;
  const int64_t player_id = 7;
  const int64_t buy_in = 150;

  ASSERT_TRUE(ledger->DebitBuyIn(player_id, buy_in, "missing_table").success);
  EXPECT_FALSE(game.JoinTable(static_cast<int32_t>(player_id), "missing_table", "Alice", -1, buy_in, ""));

  auto refund = ledger->CreditRefund(player_id, buy_in, "missing_table:rollback");
  ASSERT_TRUE(refund.success);
  EXPECT_EQ(refund.balance_after, 1000);
  EXPECT_EQ(ledger->GetBalance(player_id), 1000);
}

TEST(TournamentEconomyTest, JoinDebitsEntryCost) {
  Database db;
  AccountRepository* accounts = nullptr;
  ChipLedger* ledger = nullptr;
  const std::string path = "/tmp/test_economy_tourn.db";
  std::filesystem::remove(path);
  ASSERT_TRUE(db.Open(path));
  SetupWalletSchema(db, &accounts, &ledger);
  ASSERT_TRUE(accounts->SaveAccount(MakeAccount(9, 500)));

  TournamentServer server;
  TournamentConfig config = TournamentBuilder()
                                .WithName("Paid Tourney")
                                .WithBuyIn(10.0, 5.0)
                                .WithMaxPlayers(10)
                                .Build();
  const int tid = server.CreateTournament(config);
  ASSERT_EQ(server.GetTournamentEntryCost(tid), 15.0);

  auto debit = ledger->DebitBuyIn(9, 15, "tournament:" + std::to_string(tid));
  ASSERT_TRUE(debit.success);
  EXPECT_EQ(debit.balance_after, 485);
  EXPECT_TRUE(server.JoinTournament(tid, 9, "Bob"));

  delete ledger;
  delete accounts;
  db.Close();
  std::filesystem::remove(path);
}


TEST(TournamentEconomyTest, LeaveRefundsEntryDuringRegistration) {
  Database db;
  AccountRepository* accounts = nullptr;
  ChipLedger* ledger = nullptr;
  const std::string path = "/tmp/test_economy_tourn_leave.db";
  std::filesystem::remove(path);
  ASSERT_TRUE(db.Open(path));
  SetupWalletSchema(db, &accounts, &ledger);
  ASSERT_TRUE(accounts->SaveAccount(MakeAccount(11, 300)));

  TournamentServer server;
  TournamentConfig config = TournamentBuilder().WithName("Refund Tourney").WithBuyIn(20.0, 5.0).Build();
  const int tid = server.CreateTournament(config);

  ASSERT_TRUE(ledger->DebitBuyIn(11, 25, "tournament:" + std::to_string(tid)).success);
  ASSERT_TRUE(server.JoinTournament(tid, 11, "Carol"));

  auto leave = server.LeaveTournament(tid, 11);
  ASSERT_TRUE(leave.success);
  EXPECT_EQ(leave.refund_amount, 25);

  ASSERT_TRUE(ledger->CreditRefund(11, leave.refund_amount, "tournament:" + std::to_string(tid) + ":leave").success);
  EXPECT_EQ(ledger->GetBalance(11), 300);

  delete ledger;
  delete accounts;
  db.Close();
  std::filesystem::remove(path);
}
