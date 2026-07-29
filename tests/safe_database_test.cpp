#include "poker_engine/persistence/safe_database.h"

#include <gtest/gtest.h>

#include <filesystem>

#include "poker_engine/base/result.h"
#include "poker_engine/persistence/player_repository_safe.h"
#include "poker_engine/persistence/safe_query.h"

using namespace poker_engine::persistence;
using namespace poker_engine::base;

class SafeDatabaseTest : public ::testing::Test {
 protected:
  std::string test_db_path_;
  SafeDatabase* db_;

  void SetUp() override {
    test_db_path_ = "/tmp/poker_test/test.db";
    std::filesystem::create_directories("/tmp/poker_test");
    std::filesystem::remove(test_db_path_);
    db_ = new SafeDatabase(test_db_path_);

    auto result = db_->Connect();
    ASSERT_TRUE(result.IsOk()) << "Failed to connect to test DB";

    db_->Execute(
        SafeQuery("CREATE TABLE IF NOT EXISTS test_players ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  "username TEXT UNIQUE NOT NULL,"
                  "display_name TEXT NOT NULL,"
                  "chips INTEGER DEFAULT 1000,"
                  "total_profit INTEGER DEFAULT 0,"
                  "hands_played INTEGER DEFAULT 0,"
                  "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)"));
  }

  void TearDown() override {
    delete db_;
    std::filesystem::remove(test_db_path_);
  }
};

// ==================== Basic CRUD ====================

TEST_F(SafeDatabaseTest, SafeQueryCreateAndRead) {
  auto result =
      db_->Execute(SafeQuery("INSERT INTO test_players (username, display_name) VALUES (?, ?)")
                       .Bind("testuser1")
                       .Bind("Test User 1"));

  ASSERT_TRUE(result.IsOk());
  int64_t id = result.Unwrap();
  EXPECT_GT(id, 0);

  auto query_result = db_->Query(
      SafeQuery("SELECT username, display_name FROM test_players WHERE id = ?").Bind(id));

  ASSERT_TRUE(query_result.IsOk());
  auto rows = query_result.Unwrap();
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0][0], "testuser1");
  EXPECT_EQ(rows[0][1], "Test User 1");
}

TEST_F(SafeDatabaseTest, SQLInjectionPrevented) {
  std::string malicious_username = "'; DROP TABLE test_players; --";

  auto result =
      db_->Execute(SafeQuery("INSERT INTO test_players (username, display_name) VALUES (?, ?)")
                       .Bind(malicious_username)
                       .Bind("Malicious User"));

  ASSERT_TRUE(result.IsOk());

  auto check = db_->Query(SafeQuery("SELECT COUNT(*) FROM test_players"));
  ASSERT_TRUE(check.IsOk());

  // The key test: table still exists (DROP was not executed)
  auto verify = db_->Query(SafeQuery("SELECT COUNT(*) FROM test_players"));
  ASSERT_TRUE(verify.IsOk());
  EXPECT_GE(std::stoll(verify.Unwrap()[0][0]), 1);  // row was inserted, not dropped

  // The malicious string was stored as literal text (not SQL)
  auto stored = db_->Query(SafeQuery("SELECT username FROM test_players WHERE id = ?").Bind(1));
  ASSERT_TRUE(stored.IsOk());
  EXPECT_TRUE(stored.Unwrap()[0][0].find("DROP") != std::string::npos);  // literal contains DROP
}

TEST_F(SafeDatabaseTest, TransactionCommit) {
  auto txn = db_->BeginTransaction();

  auto result =
      db_->Execute(SafeQuery("INSERT INTO test_players (username, display_name) VALUES (?, ?)")
                       .Bind("txn_user")
                       .Bind("Transaction User"));

  ASSERT_TRUE(result.IsOk());
  ASSERT_TRUE(txn.Commit().IsOk());

  auto check = db_->Query(
      SafeQuery("SELECT display_name FROM test_players WHERE username = ?").Bind("txn_user"));
  ASSERT_TRUE(check.IsOk());
  EXPECT_EQ(check.Unwrap()[0][0], "Transaction User");
}

TEST_F(SafeDatabaseTest, TransactionRollback) {
  auto txn = db_->BeginTransaction();

  db_->Execute(SafeQuery("INSERT INTO test_players (username, display_name) VALUES (?, ?)")
                   .Bind("rollback_user")
                   .Bind("Rollback User"));

  txn.Rollback();

  auto check =
      db_->Query(SafeQuery("SELECT id FROM test_players WHERE username = ?").Bind("rollback_user"));
  ASSERT_TRUE(check.IsOk());
  EXPECT_TRUE(check.Unwrap().empty());
}

TEST_F(SafeDatabaseTest, PreparedStatementBindings) {
  db_->Execute(
      SafeQuery("INSERT INTO test_players (username, display_name, chips) VALUES (?, ?, ?)")
          .Bind("pstmt_user")
          .Bind("Prepared User")
          .Bind(5000));

  auto prep = db_->Prepare("SELECT chips, display_name FROM test_players WHERE username = ?");

  ASSERT_TRUE(prep.IsOk());
  auto& stmt = prep.Unwrap();
  stmt.Bind(0, std::string("pstmt_user"));

  ASSERT_TRUE(stmt.Step());
  EXPECT_EQ(stmt.GetInt(0), 5000);
  EXPECT_EQ(stmt.GetString(1), "Prepared User");

  stmt.Reset();
  stmt.Bind(0, std::string("nonexistent"));
  EXPECT_FALSE(stmt.Step());
}

// ==================== SafePlayerRepository Tests ====================

class SafePlayerRepositoryTest : public ::testing::Test {
 protected:
  SafeDatabase* db_;
  SafePlayerRepository* repo_;

  void SetUp() override {
    std::filesystem::create_directories("/tmp/poker_test");
    std::filesystem::remove("/tmp/poker_test/player_repo.db");
    db_ = new SafeDatabase("/tmp/poker_test/player_repo.db");
    ASSERT_TRUE(db_->Connect().IsOk());

    db_->Execute(
        SafeQuery("CREATE TABLE IF NOT EXISTS players ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  "username TEXT UNIQUE NOT NULL,"
                  "display_name TEXT NOT NULL,"
                  "password_hash TEXT NOT NULL,"
                  "chips INTEGER DEFAULT 1000,"
                  "total_profit INTEGER DEFAULT 0,"
                  "hands_played INTEGER DEFAULT 0,"
                  "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)"));

    repo_ = new SafePlayerRepository(*db_);
  }

  void TearDown() override {
    delete repo_;
    delete db_;
    std::filesystem::remove("/tmp/poker_test/player_repo.db");
  }
};

TEST_F(SafePlayerRepositoryTest, CreateAndGetPlayer) {
  auto result = repo_->Create("test_player", "Test Player", "hashed_password_123");
  ASSERT_TRUE(result.IsOk());

  int64_t player_id = result.Unwrap();
  EXPECT_GT(player_id, 0);

  auto player = repo_->GetById(player_id);
  ASSERT_TRUE(player.IsOk());
  ASSERT_TRUE(player.Unwrap().has_value());
  EXPECT_EQ(player.Unwrap()->username, "test_player");
  EXPECT_EQ(player.Unwrap()->display_name, "Test Player");
  EXPECT_EQ(player.Unwrap()->chips, 1000);
  EXPECT_EQ(player.Unwrap()->hands_played, 0);
}

TEST_F(SafePlayerRepositoryTest, DuplicateUsernameRejected) {
  repo_->Create("unique_user", "User One", "hash1");

  auto result = repo_->Create("unique_user", "User Two", "hash2");
  EXPECT_FALSE(result.IsOk());
}

TEST_F(SafePlayerRepositoryTest, GetByUsername) {
  repo_->Create("search_user", "Searchable User", "hash");

  auto player = repo_->GetByUsername("search_user");
  ASSERT_TRUE(player.IsOk());
  ASSERT_TRUE(player.Unwrap().has_value());
  EXPECT_EQ(player.Unwrap()->display_name, "Searchable User");
}

TEST_F(SafePlayerRepositoryTest, SearchByName) {
  repo_->Create("player_alpha", "Alpha Player", "hash");
  repo_->Create("player_beta", "Beta Player", "hash");
  repo_->Create("player_gamma", "Gamma Player", "hash");

  auto results = repo_->SearchByName("alpha");
  ASSERT_TRUE(results.IsOk());
  EXPECT_EQ(results.Unwrap().size(), 1u);

  results = repo_->SearchByName("player");
  ASSERT_TRUE(results.IsOk());
  EXPECT_GE(results.Unwrap().size(), 3u);
}

TEST_F(SafePlayerRepositoryTest, AdjustChips) {
  repo_->Create("chip_player", "Chip Player", "hash");
  auto player = repo_->GetByUsername("chip_player");
  ASSERT_TRUE(player.IsOk());
  ASSERT_TRUE(player.Unwrap().has_value());

  int64_t player_id = player.Unwrap()->player_id;

  auto new_balance = repo_->AdjustChips(player_id, 500);
  ASSERT_TRUE(new_balance.IsOk());
  EXPECT_EQ(new_balance.Unwrap(), 1500);

  new_balance = repo_->AdjustChips(player_id, -300);
  ASSERT_TRUE(new_balance.IsOk());
  EXPECT_EQ(new_balance.Unwrap(), 1200);
}

TEST_F(SafePlayerRepositoryTest, InputValidation) {
  auto result = repo_->Create("ab", "Display", "hash");
  EXPECT_FALSE(result.IsOk());

  result = repo_->Create("user<script>", "Bad", "hash");
  EXPECT_FALSE(result.IsOk());
}
