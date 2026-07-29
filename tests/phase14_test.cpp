#include <gtest/gtest.h>

#include <filesystem>

#include "poker_engine/phase14/database.h"
#include "poker_engine/phase14/database_manager.h"
#include "poker_engine/phase14/hand_repository.h"
#include "poker_engine/phase14/migration.h"
#include "poker_engine/phase14/player_repository.h"
#include "poker_engine/phase14/query_builder.h"
#include "poker_engine/phase14/stat_repository.h"

using namespace poker_engine::phase14;

class Phase14Test : public ::testing::Test {
 protected:
  Database db_;
  void SetUp() override {
    std::filesystem::remove("/tmp/test_poker_phase14.db");
    ASSERT_TRUE(db_.Open("/tmp/test_poker_phase14.db"));
    MigrationManager mgr(db_);
    mgr.AddMigration(1, "Core tables", R"(
            CREATE TABLE hands (
                hand_id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id INTEGER DEFAULT 0,
                timestamp TEXT DEFAULT (datetime('now')),
                table_name TEXT,
                num_players INTEGER,
                small_blind REAL,
                big_blind REAL,
                ante REAL,
                community_cards TEXT,
                duration_ms INTEGER,
                rng_proof TEXT DEFAULT ''
            );
            CREATE TABLE player_results (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                hand_id INTEGER,
                player_id INTEGER,
                player_name TEXT,
                hole_cards TEXT,
                action_summary TEXT,
                amount_won REAL DEFAULT 0,
                amount_wagered REAL DEFAULT 0,
                net_profit REAL DEFAULT 0,
                won INTEGER DEFAULT 0,
                best_hand TEXT,
                hand_rank INTEGER,
                is_hero INTEGER DEFAULT 0
            );
            CREATE TABLE actions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                hand_id INTEGER,
                player_id INTEGER,
                street INTEGER,
                action_type TEXT,
                amount REAL,
                pot_after REAL,
                action_number INTEGER,
                timestamp TEXT DEFAULT (datetime('now'))
            );
            CREATE TABLE players (
                player_id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                display_name TEXT,
                total_buy_in REAL DEFAULT 0,
                total_cash_out REAL DEFAULT 0,
                hands_played INTEGER DEFAULT 0,
                hands_won INTEGER DEFAULT 0,
                total_net REAL DEFAULT 0,
                created_at TEXT DEFAULT (datetime('now')),
                last_seen TEXT DEFAULT (datetime('now')),
                notes TEXT
            );
            CREATE INDEX idx_hands_timestamp ON hands(timestamp);
            CREATE INDEX idx_pr_hand ON player_results(hand_id);
            CREATE INDEX idx_pr_player ON player_results(player_id);
        )");
    mgr.MigrateToLatest();
  }

  void TearDown() override {
    db_.Close();
    std::filesystem::remove("/tmp/test_poker_phase14.db");
  }
};

// ============ DATABASE ============

TEST_F(Phase14Test, DatabaseOpenClose) {
  EXPECT_TRUE(db_.IsOpen());
  EXPECT_EQ(db_.GetFilePath(), "/tmp/test_poker_phase14.db");
  db_.Close();
  EXPECT_FALSE(db_.IsOpen());
}

TEST_F(Phase14Test, DatabaseExecuteSQL) {
  std::string err;
  EXPECT_TRUE(db_.Execute("CREATE TABLE test_tbl (id INTEGER PRIMARY KEY)", err));
  EXPECT_TRUE(err.empty());
  EXPECT_FALSE(db_.Execute("CREATE TABLE test_tbl (id INTEGER PRIMARY KEY)", err));
  EXPECT_FALSE(err.empty());
}

TEST_F(Phase14Test, DatabaseQuery) {
  db_.Execute("INSERT INTO hands (table_name) VALUES ('TestTable')");
  db_.Execute("INSERT INTO hands (table_name) VALUES ('TestTable2')");
  auto rs = db_.Query("SELECT COUNT(*) FROM hands");
  ASSERT_TRUE(rs.Next());
  EXPECT_EQ(rs.GetRow().GetInt(0), 2);
}

TEST_F(Phase14Test, DatabaseTransaction) {
  db_.BeginTransaction();
  db_.Execute("INSERT INTO hands (table_name) VALUES ('TransTest')");
  db_.Commit();
  auto rs = db_.Query("SELECT COUNT(*) FROM hands WHERE table_name = 'TransTest'");
  ASSERT_TRUE(rs.Next());
  EXPECT_EQ(rs.GetRow().GetInt(0), 1);
}

TEST_F(Phase14Test, DatabaseTransactionRollback) {
  db_.BeginTransaction();
  db_.Execute("INSERT INTO hands (table_name) VALUES ('RollbackTest')");
  db_.Rollback();
  auto rs = db_.Query("SELECT COUNT(*) FROM hands WHERE table_name = 'RollbackTest'");
  ASSERT_TRUE(rs.Next());
  EXPECT_EQ(rs.GetRow().GetInt(0), 0);
}

TEST_F(Phase14Test, DatabaseWALMode) {
  db_.EnableJournalMode("WAL");
  auto rs = db_.Query("PRAGMA journal_mode");
  ASSERT_TRUE(rs.Next());
  EXPECT_EQ(rs.GetRow().GetString(0), "wal");
}

TEST_F(Phase14Test, ScopedTransactionCommit) {
  {
    ScopedTransaction tx(db_);
    db_.Execute("INSERT INTO hands (table_name) VALUES ('ScopedTest')");
    tx.Commit();
  }
  auto rs = db_.Query("SELECT COUNT(*) FROM hands WHERE table_name = 'ScopedTest'");
  ASSERT_TRUE(rs.Next());
  EXPECT_EQ(rs.GetRow().GetInt(0), 1);
}

TEST_F(Phase14Test, ScopedTransactionRollback) {
  {
    ScopedTransaction tx(db_);
    db_.Execute("INSERT INTO hands (table_name) VALUES ('ScopedRollback')");
    tx.Rollback();
  }
  auto rs = db_.Query("SELECT COUNT(*) FROM hands WHERE table_name = 'ScopedRollback'");
  ASSERT_TRUE(rs.Next());
  EXPECT_EQ(rs.GetRow().GetInt(0), 0);
}

TEST_F(Phase14Test, ResultSetIteration) {
  db_.Execute("INSERT INTO hands (hand_id, session_id, timestamp, table_name, num_players, small_blind, big_blind, ante, community_cards, duration_ms) VALUES (1, 0, '2024-01-01', 'T1', 3, 1, 2, 0, '', 100)");
  db_.Execute("INSERT INTO hands (hand_id, session_id, timestamp, table_name, num_players, small_blind, big_blind, ante, community_cards, duration_ms) VALUES (2, 0, '2024-01-02', 'T2', 4, 1, 2, 0, '', 200)");
  auto rs = db_.Query("SELECT hand_id, table_name, num_players FROM hands ORDER BY hand_id");
  int count = 0;
  while (rs.Next()) {
    Row row = rs.GetRow();
    EXPECT_EQ(row.GetInt(0), count + 1);
    EXPECT_FALSE(row.GetString(1).empty());
    count++;
  }
  EXPECT_EQ(count, 2);
}

TEST_F(Phase14Test, RowAccessors) {
  db_.Execute(
      "INSERT INTO hands (hand_id, session_id, timestamp, table_name, num_players, small_blind, big_blind, ante, community_cards, duration_ms) VALUES (1, 0, '2024-01-01', 'Test', 5, 1.5, 3.0, 0.25, 'AhKh', 150)");
  auto rs = db_.Query("SELECT * FROM hands WHERE hand_id = 1");
  ASSERT_TRUE(rs.Next());
  Row row = rs.GetRow();
  EXPECT_EQ(row.GetInt(0), 1);
  EXPECT_EQ(row.GetInt(1), 0);
  EXPECT_EQ(row.GetString(2), "2024-01-01");
  EXPECT_EQ(row.GetString(3), "Test");
  EXPECT_EQ(row.GetInt(4), 5);
  EXPECT_NEAR(row.GetDouble(5), 1.5, 0.001);
  EXPECT_NEAR(row.GetDouble(6), 3.0, 0.001);
  EXPECT_NEAR(row.GetDouble(7), 0.25, 0.001);
  EXPECT_EQ(row.GetString(8), "AhKh");
  EXPECT_EQ(row.GetInt64(9), 150);
}

// ============ HAND REPOSITORY ============

TEST_F(Phase14Test, SaveAndRetrieveHand) {
  HandRepository hand_repo(db_);
  HandRecord hand;
  hand.session_id = 1;
  hand.table_name = "TestTable";
  hand.num_players = 4;
  hand.small_blind = 1;
  hand.big_blind = 2;
  hand.community_cards = {"Ah", "Ks", "7d", "3c", "2h"};
  hand.duration_ms = 5000;

  std::vector<PlayerHandResult> results(2);
  results[0].player_id = 1;
  results[0].player_name = "Alice";
  results[0].hole_cards = "AhKs";
  results[0].net_profit = 15;
  results[0].won = true;
  results[0].best_hand = "Two Pair";
  results[0].hand_rank = 3;
  results[0].is_hero = true;
  results[1].player_id = 2;
  results[1].player_name = "Bob";
  results[1].hole_cards = "7c7d";
  results[1].net_profit = -15;
  results[1].won = false;
  results[1].best_hand = "Pair";
  results[1].hand_rank = 2;

  std::vector<ActionRecord> actions(3);
  actions[0] = {0, 1, 1, 0, "BET", 7, 7, 1, ""};
  actions[1] = {0, 1, 2, 0, "CALL", 7, 14, 2, ""};
  actions[2] = {0, 1, 1, 1, "BET", 15, 29, 3, ""};

  int64_t hand_id = hand_repo.SaveHand(hand, results, actions);
  EXPECT_GT(hand_id, 0);
  EXPECT_EQ(hand_repo.GetHandCount(), 1);

  auto retrieved = hand_repo.GetHand(hand_id);
  EXPECT_EQ(retrieved.first.hand_id, hand_id);
  EXPECT_EQ(retrieved.first.table_name, "TestTable");
  EXPECT_EQ(retrieved.first.num_players, 4);
  EXPECT_EQ(retrieved.first.community_cards.size(), 5);
  EXPECT_EQ(retrieved.second.size(), 2);
  EXPECT_EQ(retrieved.second[0].player_name, "Alice");
  EXPECT_TRUE(retrieved.second[0].won);
  EXPECT_EQ(retrieved.second[1].player_name, "Bob");
  EXPECT_FALSE(retrieved.second[1].won);

  auto action_list = hand_repo.GetHandActions(hand_id);
  EXPECT_EQ(action_list.size(), 3);
  EXPECT_EQ(action_list[0].action_type, "BET");
  EXPECT_EQ(action_list[1].action_type, "CALL");
}

TEST_F(Phase14Test, ListHands) {
  HandRepository hand_repo(db_);
  for (int i = 1; i <= 5; i++) {
    HandRecord hand;
    hand.session_id = i;
    hand.table_name = "Table" + std::to_string(i);
    hand.num_players = 3;
    hand.small_blind = 1;
    hand.big_blind = 2;
    hand_repo.SaveHand(hand, {}, {});
  }
  auto hands = hand_repo.ListHands(3, 0);
  EXPECT_EQ(hands.size(), 3);
  auto hands2 = hand_repo.ListHands(3, 3);
  EXPECT_EQ(hands2.size(), 2);
}

TEST_F(Phase14Test, GetPlayerHands) {
  HandRepository hand_repo(db_);
  for (int h = 1; h <= 10; h++) {
    HandRecord hand;
    hand.table_name = "Test";
    hand.num_players = 2;
    hand.small_blind = 1;
    hand.big_blind = 2;
    std::vector<PlayerHandResult> results;
    for (int p = 1; p <= 2; p++) {
      PlayerHandResult r;
      r.player_id = p;
      r.player_name = "Player" + std::to_string(p);
      r.hole_cards = (p == 1) ? "AsKs" : "QhJh";
      r.net_profit = (h % 2 == 0) ? 10 : -5;
      r.won = (h % 2 == 0);
      results.push_back(r);
    }
    hand_repo.SaveHand(hand, results, {});
  }
  auto hands = hand_repo.GetPlayerHands(1, 5);
  EXPECT_EQ(hands.size(), 5);
}

// ============ PLAYER REPOSITORY ============

TEST_F(Phase14Test, CreatePlayer) {
  PlayerRepository player_repo(db_);
  int32_t id = player_repo.CreatePlayer("Alice");
  EXPECT_GT(id, 0);
  auto info = player_repo.GetPlayer(id);
  EXPECT_EQ(info.player_id, id);
  EXPECT_EQ(info.name, "Alice");
}

TEST_F(Phase14Test, CreatePlayerWithDisplayName) {
  PlayerRepository player_repo(db_);
  int32_t id = player_repo.CreatePlayer("alice_42", "Alice Smith");
  auto info = player_repo.GetPlayer(id);
  EXPECT_EQ(info.display_name, "Alice Smith");
}

TEST_F(Phase14Test, FindPlayers) {
  PlayerRepository player_repo(db_);
  player_repo.CreatePlayer("AliceWonder");
  player_repo.CreatePlayer("BobBuilder");
  player_repo.CreatePlayer("Charlie");
  auto results = player_repo.FindPlayers("Ali");
  EXPECT_GE(results.size(), 1);
  EXPECT_EQ(results[0].name, "AliceWonder");
}

TEST_F(Phase14Test, UpdatePlayerStats) {
  PlayerRepository player_repo(db_);
  int32_t id = player_repo.CreatePlayer("TestPlayer");
  player_repo.UpdatePlayerStats(id, 500, 600);
  auto info = player_repo.GetPlayer(id);
  EXPECT_DOUBLE_EQ(info.total_buy_in, 500);
  EXPECT_DOUBLE_EQ(info.total_cash_out, 600);
  player_repo.UpdatePlayerStats(id, 200, 150);
  info = player_repo.GetPlayer(id);
  EXPECT_DOUBLE_EQ(info.total_buy_in, 700);
  EXPECT_DOUBLE_EQ(info.total_cash_out, 750);
}

TEST_F(Phase14Test, UniquePlayerNames) {
  PlayerRepository player_repo(db_);
  int32_t id1 = player_repo.CreatePlayer("UniquePlayer");
  EXPECT_GT(id1, 0);
  // Duplicate name should fail
  int32_t id2 = player_repo.CreatePlayer("UniquePlayer");
  EXPECT_EQ(id2, -1);
}

// ============ STAT REPOSITORY ============

TEST_F(Phase14Test, OverviewStats) {
  HandRepository hand_repo(db_);
  PlayerRepository player_repo(db_);
  StatRepository stat_repo(db_, hand_repo, player_repo);
  auto ov = stat_repo.GetOverviewStats();
  EXPECT_EQ(ov.total_hands, 0);

  HandRecord hand;
  hand.table_name = "Test";
  hand.num_players = 2;
  hand.small_blind = 1;
  hand.big_blind = 2;
  PlayerHandResult r;
  r.player_id = 1;
  r.player_name = "TestPlayer";
  r.net_profit = 50;
  r.won = true;
  hand_repo.SaveHand(hand, {r}, {});

  ov = stat_repo.GetOverviewStats();
  EXPECT_EQ(ov.total_hands, 1);
  EXPECT_EQ(ov.total_wins, 1);
}

TEST_F(Phase14Test, VarianceStats) {
  HandRepository hand_repo(db_);
  PlayerRepository player_repo(db_);
  StatRepository stat_repo(db_, hand_repo, player_repo);
  int32_t pid = player_repo.CreatePlayer("VariPlayer");

  for (int i = 0; i < 20; i++) {
    HandRecord hand;
    hand.table_name = "Test";
    hand.num_players = 2;
    hand.small_blind = 1;
    hand.big_blind = 2;
    PlayerHandResult r;
    r.player_id = pid;
    r.player_name = "VariPlayer";
    r.hole_cards = (i % 2 == 0) ? "AsKs" : "72o";
    r.net_profit = (i % 2 == 0) ? 10.0 : -5.0;
    r.won = (i % 2 == 0);
    r.amount_won = r.won ? 20.0 : 0;
    r.amount_wagered = 10.0;
    hand_repo.SaveHand(hand, {r}, {});
  }

  auto vs = stat_repo.GetVarianceStats(pid);
  EXPECT_EQ(vs.sample_size, 20);
  EXPECT_GT(vs.std_dev_bb100, 0);
  EXPECT_LT(vs.ci95_low, vs.ci95_high);
  // 10 wins at +10, 10 losses at -5 => mean = (100 - 50)/20 = 2.5
  EXPECT_NEAR(vs.mean_bb100, 2.5, 0.5);
}

TEST_F(Phase14Test, HandTypeStats) {
  HandRepository hand_repo(db_);
  PlayerRepository player_repo(db_);
  StatRepository stat_repo(db_, hand_repo, player_repo);
  int32_t pid = player_repo.CreatePlayer("HandTypePlayer");

  std::vector<std::pair<std::string, double>> test_hands = {
      {"AsKs", 20}, {"QhQd", 15}, {"JcTc", 5}, {"72o", -10}, {"32o", -8}, {"AhKh", 25}, {"Th9h", 3},
  };

  for (auto& [hole, profit] : test_hands) {
    HandRecord hand;
    hand.table_name = "Test";
    hand.num_players = 2;
    hand.small_blind = 1;
    hand.big_blind = 2;
    PlayerHandResult r;
    r.player_id = pid;
    r.player_name = "HandTypePlayer";
    r.hole_cards = hole;
    r.net_profit = profit;
    r.won = profit > 0;
    r.amount_wagered = 10;
    hand_repo.SaveHand(hand, {r}, {});
  }

  auto type_stats = stat_repo.GetHandTypeStats(pid);
  EXPECT_GT(type_stats.size(), 0);
}

// ============ QUERY BUILDER ============

TEST_F(Phase14Test, QueryBuilderBasic) {
  QueryBuilder qb("hands");
  qb.Select("hand_id, table_name, num_players").Where("num_players > 2").OrderBy("hand_id DESC");
  std::string sql = qb.Build();
  EXPECT_NE(sql.find("SELECT"), std::string::npos);
  EXPECT_NE(sql.find("hands"), std::string::npos);
  EXPECT_NE(sql.find("num_players > 2"), std::string::npos);
  EXPECT_NE(sql.find("ORDER BY"), std::string::npos);
}

TEST_F(Phase14Test, QueryBuilderWithJoin) {
  QueryBuilder qb("player_results");
  qb.Select("pr.player_id, p.name, pr.amount_won")
      .Join("players p", "pr.player_id = p.player_id")
      .Where("pr.won = 1")
      .GroupBy("pr.player_id")
      .Limit(10);
  std::string sql = qb.Build();
  EXPECT_NE(sql.find("JOIN"), std::string::npos);
  EXPECT_NE(sql.find("GROUP BY"), std::string::npos);
  EXPECT_NE(sql.find("LIMIT 10"), std::string::npos);
}

TEST_F(Phase14Test, QueryBuilderAggregate) {
  QueryBuilder qb("player_results");
  qb.Select("player_id")
      .Count("hand_id")
      .Sum("net_profit", "total_net")
      .GroupBy("player_id")
      .OrderByDesc("total_net");
  std::string sql = qb.Build();
  EXPECT_NE(sql.find("COUNT"), std::string::npos);
  EXPECT_NE(sql.find("SUM"), std::string::npos);
}

// ============ DATABASE MANAGER ============

TEST_F(Phase14Test, DatabaseManagerInit) {
  std::string test_path = "/tmp/test_poker_mgr.db";
  std::filesystem::remove(test_path);
  auto& mgr = DatabaseManager::Instance();
  ASSERT_TRUE(mgr.Initialize(test_path));
  EXPECT_TRUE(mgr.IsHealthy());
  mgr.Shutdown();
  std::filesystem::remove(test_path);
}

TEST_F(Phase14Test, PlayerReport) {
  std::string test_path = "/tmp/test_poker_report.db";
  std::filesystem::remove(test_path);
  auto& mgr = DatabaseManager::Instance();
  ASSERT_TRUE(mgr.Initialize(test_path));

  int32_t pid = mgr.GetPlayerRepo().CreatePlayer("ReportTest");
  for (int i = 0; i < 5; i++) {
    HandRecord hand;
    hand.table_name = "TestTable";
    hand.num_players = 2;
    hand.small_blind = 1;
    hand.big_blind = 2;
    PlayerHandResult r;
    r.player_id = pid;
    r.player_name = "ReportTest";
    r.hole_cards = (i < 3) ? "AKs" : "72o";
    r.net_profit = (i < 3) ? 15.0 : -10.0;
    r.won = i < 3;
    r.amount_wagered = 10;
    mgr.GetHandRepo().SaveHand(hand, {r}, {});
  }

  std::string report = mgr.GetPlayerReport(pid);
  EXPECT_NE(report.find("ReportTest"), std::string::npos);
  EXPECT_NE(report.find("Variance"), std::string::npos);
  mgr.Shutdown();
  std::filesystem::remove(test_path);
}

TEST_F(Phase14Test, LeaderboardJson) {
  std::string test_path = "/tmp/test_poker_lb.db";
  std::filesystem::remove(test_path);
  auto& mgr = DatabaseManager::Instance();
  ASSERT_TRUE(mgr.Initialize(test_path));
  for (int i = 1; i <= 5; i++) {
    int32_t pid = mgr.GetPlayerRepo().CreatePlayer("Player" + std::to_string(i));
    mgr.GetPlayerRepo().UpdatePlayerStats(pid, 100 * i, 100 * i + i * 50);
    // Add hand data so player appears in leaderboard
    HandRecord hand;
    hand.table_name = "Test";
    hand.num_players = 2;
    hand.small_blind = 1;
    hand.big_blind = 2;
    PlayerHandResult r;
    r.player_id = pid;
    r.player_name = "Player" + std::to_string(i);
    r.hole_cards = "AsKs";
    r.net_profit = i * 10;
    r.won = true;
    r.amount_wagered = 10;
    mgr.GetHandRepo().SaveHand(hand, {r}, {});
  }
  std::string json = mgr.GetLeaderboardJSON(5);
  EXPECT_NE(json.find("Player"), std::string::npos);
  mgr.Shutdown();
  std::filesystem::remove(test_path);
}

TEST_F(Phase14Test, BackupDatabase) {
  std::string test_path = "/tmp/test_poker_backup.db";
  std::string backup_path = "/tmp/test_backup_out.db";
  std::filesystem::remove(test_path);
  std::filesystem::remove(backup_path);

  auto& mgr = DatabaseManager::Instance();
  ASSERT_TRUE(mgr.Initialize(test_path));
  mgr.GetPlayerRepo().CreatePlayer("BackupTestPlayer");

  bool ok = mgr.Backup(backup_path);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(std::filesystem::exists(backup_path));
  EXPECT_GT(std::filesystem::file_size(backup_path), 0u);

  mgr.Shutdown();
  std::filesystem::remove(test_path);
  std::filesystem::remove(backup_path);
}

// ============ INTEGRATION ============

TEST_F(Phase14Test, FullPipelineIntegration) {
  std::string test_path = "/tmp/test_poker_full.db";
  std::filesystem::remove(test_path);
  auto& mgr = DatabaseManager::Instance();
  ASSERT_TRUE(mgr.Initialize(test_path));

  int32_t p1 = mgr.GetPlayerRepo().CreatePlayer("Alice");
  int32_t p2 = mgr.GetPlayerRepo().CreatePlayer("Bob");
  int32_t p3 = mgr.GetPlayerRepo().CreatePlayer("Charlie");

  for (int h = 0; h < 20; h++) {
    HandRecord hand;
    hand.session_id = 1;
    hand.table_name = "IntegrationTest";
    hand.num_players = 3;
    hand.small_blind = 1;
    hand.big_blind = 2;
    hand.community_cards = {"Ah", "Ks", "7d"};

    std::vector<PlayerHandResult> results;
    int winner = h % 3;
    int32_t pids[] = {p1, p2, p3};
    for (int i = 0; i < 3; i++) {
      PlayerHandResult r;
      r.player_id = pids[i];
      r.player_name = (i == 0) ? "Alice" : (i == 1) ? "Bob" : "Charlie";
      r.hole_cards = (i == 0) ? "AsKs" : (i == 1) ? "QhJh" : "7c7d";
      r.net_profit = (i == winner) ? 20.0 : -10.0;
      r.won = (i == winner);
      r.amount_wagered = 10;
      results.push_back(r);
    }
    mgr.GetHandRepo().SaveHand(hand, results, {});
  }

  EXPECT_EQ(mgr.GetHandRepo().GetHandCount(), 20);

  auto var_stats = mgr.GetStatRepo().GetVarianceStats(p1);
  EXPECT_EQ(var_stats.sample_size, 20);

  auto opp_stats = mgr.GetStatRepo().GetOpponentStats(p1);
  EXPECT_EQ(opp_stats.size(), 2);

  auto json = mgr.GetLeaderboardJSON(3);
  EXPECT_NE(json.find("Alice"), std::string::npos);

  mgr.Shutdown();
  std::filesystem::remove(test_path);
}
