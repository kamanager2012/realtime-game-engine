#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

#include "poker_engine/phase14/database_manager.h"
#include "poker_engine/phase14/migration.h"

using namespace poker_engine::phase14;

#if defined(POKER_HAVE_LIBPQ)
#include <libpq-fe.h>

namespace {

int64_t QueryCount(PGconn* conn, const std::string& sql) {
  PGresult* res = PQexec(conn, sql.c_str());
  if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) < 1) {
    PQclear(res);
    return -1;
  }
  int64_t value = std::atoll(PQgetvalue(res, 0, 0));
  PQclear(res);
  return value;
}

}  // namespace
#endif

class PostgresMirrorIntegrationTest : public ::testing::Test {
 protected:
  std::string sqlite_path_ = "/tmp/test_pg_mirror_integration.db";

  void SetUp() override {
    const char* url = std::getenv("POKER_POSTGRES_URL");
    if (!url || std::string(url).empty()) {
      GTEST_SKIP() << "POKER_POSTGRES_URL not set";
    }
#if !defined(POKER_HAVE_LIBPQ)
    GTEST_SKIP() << "libpq not available";
#else
    postgres_url_ = url;
#endif
    std::filesystem::remove(sqlite_path_);
  }

  void TearDown() override {
    DatabaseManager::Instance().DisconnectPostgresMirror();
    DatabaseManager::Instance().Shutdown();
    std::filesystem::remove(sqlite_path_);
  }

#if defined(POKER_HAVE_LIBPQ)
  std::string postgres_url_;
#endif
};

TEST_F(PostgresMirrorIntegrationTest, AccountAndWalletRowsMirrored) {
#if !defined(POKER_HAVE_LIBPQ)
  GTEST_SKIP() << "libpq not available";
#else
  ASSERT_TRUE(DatabaseManager::Instance().Initialize(sqlite_path_));
  if (!DatabaseManager::Instance().ConnectPostgresMirror(postgres_url_)) {
    GTEST_SKIP() << "PostgreSQL unavailable at POKER_POSTGRES_URL";
  }

  AccountData account;
  account.id = 88001;
  account.username = "pg_mirror_user";
  account.display_name = "PG Mirror";
  account.password_hash = "salt$hash";
  account.chips = 1000;
  ASSERT_TRUE(DatabaseManager::Instance().GetAccountRepo().SaveAccount(account));

  auto debit = DatabaseManager::Instance().GetChipLedger().DebitBuyIn(88001, 200, "table_pg");
  ASSERT_TRUE(debit.success);

  PGconn* conn = PQconnectdb(postgres_url_.c_str());
  if (PQstatus(conn) != CONNECTION_OK) {
    PQfinish(conn);
    GTEST_SKIP() << "Cannot verify PostgreSQL rows";
  }

  const int64_t account_count =
      QueryCount(conn, "SELECT COUNT(*) FROM accounts WHERE id = 88001");
  const int64_t wallet_count = QueryCount(
      conn, "SELECT COUNT(*) FROM wallet_transactions WHERE player_id = 88001");
  const int64_t chips = QueryCount(conn, "SELECT chips FROM accounts WHERE id = 88001");

  PQfinish(conn);

  EXPECT_EQ(account_count, 1);
  EXPECT_GE(wallet_count, 1);
  EXPECT_EQ(chips, 800);
#endif
}
