#include <gtest/gtest.h>

#include <filesystem>

#include "poker_engine/base/result.h"
#include "poker_engine/persistence/player_repository_safe.h"
#include "poker_engine/persistence/safe_database.h"
#include "poker_engine/persistence/safe_query.h"

using namespace poker_engine::persistence;
using namespace poker_engine::base;

class PersistenceTest : public ::testing::Test {
 protected:
  SafeDatabase* db_;
  std::string db_path_ = "/tmp/poker_test/persistence_test.db";

  void SetUp() override {
    std::filesystem::create_directories("/tmp/poker_test");
    std::filesystem::remove(db_path_);
    db_ = new SafeDatabase(db_path_);
    ASSERT_TRUE(db_->Connect().IsOk());
  }

  void TearDown() override {
    delete db_;
    std::filesystem::remove(db_path_);
  }
};

TEST_F(PersistenceTest, ConnectionStateTest) { EXPECT_TRUE(db_->IsConnected()); }

TEST_F(PersistenceTest, SimpleQueryParameterized) {
  db_->Execute(SafeQuery("CREATE TABLE test(id INTEGER PRIMARY KEY, name TEXT)"));

  db_->Execute(SafeQuery("INSERT INTO test (name) VALUES (?)").Bind("Alice"));

  auto result = db_->Query(SafeQuery("SELECT name FROM test WHERE id = ?").Bind(1));

  ASSERT_TRUE(result.IsOk());
  auto rows = result.Unwrap();
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0][0], "Alice");
}

TEST_F(PersistenceTest, BatchInsert) {
  db_->Execute(SafeQuery("CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT)"));

  for (int i = 0; i < 100; ++i) {
    db_->Execute(
        SafeQuery("INSERT INTO data (value) VALUES (?)").Bind("value_" + std::to_string(i)));
  }

  auto result = db_->Query(SafeQuery("SELECT COUNT(*) FROM data"));
  ASSERT_TRUE(result.IsOk());
  EXPECT_EQ(std::stoll(result.Unwrap()[0][0]), 100);
}

TEST_F(PersistenceTest, TransactionAtomicity) {
  db_->Execute(SafeQuery("CREATE TABLE accounts(id INTEGER PRIMARY KEY, balance INTEGER)"));

  db_->Execute(SafeQuery("INSERT INTO accounts (balance) VALUES (1000)"));
  db_->Execute(SafeQuery("INSERT INTO accounts (balance) VALUES (1000)"));

  auto txn = db_->BeginTransaction();

  db_->Execute(SafeQuery("UPDATE accounts SET balance = balance - 200 WHERE id = 1"));
  db_->Execute(SafeQuery("UPDATE accounts SET balance = balance + 200 WHERE id = 2"));

  txn.Commit();

  auto result = db_->Query(SafeQuery("SELECT balance FROM accounts WHERE id = 2"));
  ASSERT_TRUE(result.IsOk());
  EXPECT_EQ(std::stoll(result.Unwrap()[0][0]), 1200);
}

TEST_F(PersistenceTest, TransactionRollback) {
  db_->Execute(SafeQuery("CREATE TABLE rollback_test(id INTEGER PRIMARY KEY, value TEXT)"));

  auto txn = db_->BeginTransaction();
  db_->Execute(SafeQuery("INSERT INTO rollback_test (value) VALUES (?)").Bind("temp"));
  txn.Rollback();

  auto result = db_->Query(SafeQuery("SELECT COUNT(*) FROM rollback_test"));
  ASSERT_TRUE(result.IsOk());
  EXPECT_EQ(std::stoll(result.Unwrap()[0][0]), 0);
}

TEST_F(PersistenceTest, PreparedStatementReuse) {
  db_->Execute(SafeQuery("CREATE TABLE stmt_test(id INTEGER PRIMARY KEY, name TEXT)"));

  auto prep = db_->Prepare("INSERT INTO stmt_test (name) VALUES (?)");
  ASSERT_TRUE(prep.IsOk());

  auto& stmt = prep.Unwrap();

  for (int i = 0; i < 5; ++i) {
    stmt.Bind(0, std::string("item_" + std::to_string(i)));
    stmt.Step();
    stmt.Reset();
  }

  auto result = db_->Query(SafeQuery("SELECT COUNT(*) FROM stmt_test"));
  ASSERT_TRUE(result.IsOk());
  EXPECT_EQ(std::stoll(result.Unwrap()[0][0]), 5);
}

TEST_F(PersistenceTest, LargeDataInsert) {
  db_->Execute(SafeQuery("CREATE TABLE big_data(id INTEGER PRIMARY KEY, payload TEXT)"));

  std::string payload(10000, 'x');

  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 100; ++i) {
    db_->Execute(SafeQuery("INSERT INTO big_data (payload) VALUES (?)").Bind(payload));
  }
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::high_resolution_clock::now() - start)
                     .count();

  auto result = db_->Query(SafeQuery("SELECT COUNT(*) FROM big_data"));
  ASSERT_TRUE(result.IsOk());
  EXPECT_EQ(std::stoll(result.Unwrap()[0][0]), 100);
}
