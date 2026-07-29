#include "poker_engine/phase14/database_manager.h"

#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::phase14 {

DatabaseManager& DatabaseManager::Instance() {
  static DatabaseManager instance;
  return instance;
}

bool DatabaseManager::Initialize(const std::string& db_path) {
  if (initialized_ && db_ && db_->IsOpen()) {
    // Already initialized - close and re-open with new path
    db_->Close();
  }

  db_ = std::make_unique<Database>();
  if (!db_->Open(db_path)) {
    std::cerr << "Failed to open database: " << db_path << "\n";
    return false;
  }

  std::cout << "Database opened: " << db_->GetFilePath() << "\n";
  std::cout << "SQLite version: " << db_->GetVersion() << "\n";

  // 运行迁移
  MigrationManager mgr(*db_);

  mgr.AddMigration(1, "Create core tables", R"(
        CREATE TABLE IF NOT EXISTS hands (
            hand_id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id INTEGER DEFAULT 0,
            timestamp TEXT DEFAULT (datetime('now')),
            table_name TEXT,
            num_players INTEGER,
            small_blind REAL,
            big_blind REAL,
            ante REAL,
            community_cards TEXT,
            duration_ms INTEGER
        );
        CREATE TABLE IF NOT EXISTS player_results (
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
        CREATE TABLE IF NOT EXISTS actions (
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
        CREATE TABLE IF NOT EXISTS players (
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
        CREATE INDEX IF NOT EXISTS idx_hands_timestamp ON hands(timestamp);
        CREATE INDEX IF NOT EXISTS idx_pr_hand ON player_results(hand_id);
        CREATE INDEX IF NOT EXISTS idx_pr_player ON player_results(player_id);
        CREATE INDEX IF NOT EXISTS idx_actions_hand ON actions(hand_id);
        CREATE INDEX IF NOT EXISTS idx_actions_player_street ON actions(player_id, street);
    )");

  mgr.AddMigration(2, "Add position tracking", R"(
        ALTER TABLE player_results ADD COLUMN position INTEGER DEFAULT -1;
        ALTER TABLE actions ADD COLUMN position INTEGER DEFAULT -1;
    )");

  mgr.AddMigration(3, "Add session tracking", R"(
        CREATE TABLE IF NOT EXISTS sessions (
            session_id INTEGER PRIMARY KEY AUTOINCREMENT,
            start_time TEXT DEFAULT (datetime('now')),
            end_time TEXT,
            table_name TEXT,
            num_hands INTEGER DEFAULT 0,
            total_buy_in REAL DEFAULT 0
        );
    )");

  mgr.AddMigration(4, "Add hand notes and tags", R"(
        ALTER TABLE hands ADD COLUMN notes TEXT;
        ALTER TABLE hands ADD COLUMN tags TEXT;
        ALTER TABLE player_results ADD COLUMN notes TEXT;
    )");

  mgr.AddMigration(5, "Add tournament support", R"(
        CREATE TABLE IF NOT EXISTS tournaments (
            tournament_id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT,
            buy_in REAL,
            started_at TEXT DEFAULT (datetime('now')),
            finished_at TEXT,
            num_players INTEGER,
            status TEXT DEFAULT 'active'
        );
        ALTER TABLE hands ADD COLUMN tournament_id INTEGER DEFAULT 0;
        ALTER TABLE players ADD COLUMN total_tournaments INTEGER DEFAULT 0;
        ALTER TABLE players ADD COLUMN total_tournament_wins INTEGER DEFAULT 0;
    )");

  mgr.AddMigration(7, "Add wallet transaction ledger", R"(
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
        CREATE INDEX IF NOT EXISTS idx_wallet_tx_player ON wallet_transactions(player_id);
        CREATE INDEX IF NOT EXISTS idx_wallet_tx_created ON wallet_transactions(created_at);
    )");

  mgr.AddMigration(8, "Add RNG fairness proof to hands", R"(
        ALTER TABLE hands ADD COLUMN rng_proof TEXT DEFAULT '';
    )");

  mgr.AddMigration(6, "Add accounts table", R"(
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
        CREATE UNIQUE INDEX IF NOT EXISTS idx_accounts_username ON accounts(username);
    )");

  mgr.AddMigration(9, "Add daily bonus table", R"(
        CREATE TABLE IF NOT EXISTS daily_bonus (
            player_id INTEGER PRIMARY KEY,
            last_claim TEXT DEFAULT '',
            streak INTEGER DEFAULT 0
        );
    )");

  mgr.AddMigration(10, "Add achievements tables", R"(
        CREATE TABLE IF NOT EXISTS achievements_def (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            desc TEXT,
            icon TEXT
        );
        CREATE TABLE IF NOT EXISTS player_achievements (
            player_id INTEGER,
            achievement_id TEXT,
            unlocked_at TEXT DEFAULT (datetime('now')),
            PRIMARY KEY (player_id, achievement_id)
        );
    )");

  mgr.MigrateToLatest();

  // 初始化仓库
  hand_repo_ = std::make_unique<HandRepository>(*db_);
  player_repo_ = std::make_unique<PlayerRepository>(*db_);
  stat_repo_ = std::make_unique<StatRepository>(*db_, *hand_repo_, *player_repo_);
  account_repo_ = std::make_unique<AccountRepository>(*db_);
  chip_ledger_ = std::make_unique<ChipLedger>(*db_);
  account_repo_->SeedAchievements();  // 首次启动种子化成就定义

  initialized_ = true;
  std::cout << "Database initialized successfully.\n";
  return true;
}

void DatabaseManager::Shutdown() {
  DisconnectPostgresMirror();
  hand_repo_.reset();
  player_repo_.reset();
  stat_repo_.reset();
  chip_ledger_.reset();
  account_repo_.reset();
  if (db_) {
    db_->Close();
    std::cout << "Database closed.\n";
  }
  db_.reset();
  initialized_ = false;
}

bool DatabaseManager::IsHealthy() const {
  if (!db_ || !db_->IsOpen()) return false;
  try {
    auto rs = db_->Query("SELECT 1");
    return rs.Next();
  } catch (const std::exception& e) {
    PE_LOG_ERROR("DatabaseManager::IsHealthy: health check failed: {}", e.what());
    return false;
  } catch (...) {
    PE_LOG_ERROR("DatabaseManager::IsHealthy: unknown error during health check");
    return false;
  }
}

HandRepository& DatabaseManager::GetHandRepo() { return *hand_repo_; }
PlayerRepository& DatabaseManager::GetPlayerRepo() { return *player_repo_; }
StatRepository& DatabaseManager::GetStatRepo() { return *stat_repo_; }
Database& DatabaseManager::GetDatabase() { return *db_; }
AccountRepository& DatabaseManager::GetAccountRepo() { return *account_repo_; }

ChipLedger& DatabaseManager::GetChipLedger() { return *chip_ledger_; }


bool DatabaseManager::ConnectPostgresMirror(const std::string& url) {
  if (url.empty()) return false;
  postgres_mirror_ = std::make_unique<PostgresMirror>();
  if (!postgres_mirror_->Connect(url)) {
    postgres_mirror_.reset();
    return false;
  }
  PostgresMirror* mirror = postgres_mirror_.get();
  if (chip_ledger_) {
    chip_ledger_->SetWalletMirrorCallback([mirror](const WalletMirrorEvent& ev) {
      if (mirror && mirror->IsConnected()) mirror->MirrorWalletEvent(ev);
    });
  }
  if (account_repo_) {
    account_repo_->SetAccountMirrorCallback([mirror](const AccountData& account) {
      if (mirror && mirror->IsConnected()) mirror->MirrorAccount(account);
    });
  }
  std::cout << "PostgreSQL mirror connected.\n";
  return true;
}

void DatabaseManager::DisconnectPostgresMirror() {
  if (chip_ledger_) chip_ledger_->SetWalletMirrorCallback(nullptr);
  if (account_repo_) account_repo_->SetAccountMirrorCallback(nullptr);
  if (postgres_mirror_) postgres_mirror_->Disconnect();
  postgres_mirror_.reset();
}

bool DatabaseManager::IsPostgresMirrorConnected() const {
  return postgres_mirror_ && postgres_mirror_->IsConnected();
}

// ========== 统计报告 ==========

std::string DatabaseManager::GetPlayerReport(int32_t player_id) {
  auto& pr = *player_repo_;
  auto& sr = *stat_repo_;

  auto info = pr.GetPlayer(player_id);
  if (info.player_id == 0) return "Player not found";

  auto stats = pr.CalculateStats(player_id);
  auto variance = sr.GetVarianceStats(player_id);
  auto opponents = sr.GetOpponentStats(player_id);

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "===== PLAYER REPORT =====\n\n";
  oss << info.ToString() << "\n\n";

  oss << "--- Performance Stats ---\n";
  oss << stats.ToString() << "\n";

  oss << "--- Variance Analysis ---\n";
  oss << "  Sample Size: " << variance.sample_size << " hands\n";
  oss << "  Mean BB/100: " << variance.mean_bb100 << "\n";
  oss << "  Std Dev: " << variance.std_dev_bb100 << "\n";
  oss << "  95% CI: [" << variance.ci95_low << ", " << variance.ci95_high << "]\n";
  oss << "  Max Drawdown: $" << int(variance.max_drawdown) << " (" << int(variance.max_drawdown_pct)
      << "%)\n";
  oss << "  Sharpe Ratio: " << variance.sharpe_ratio << "\n\n";

  if (!opponents.empty()) {
    oss << "--- Top Opponents ---\n";
    int shown = 0;
    for (const auto& opp : opponents) {
      if (shown >= 10) break;
      oss << "  vs " << std::setw(12) << opp.opponent_name << " | " << std::setw(4)
          << opp.hands_played << " hands"
          << " | " << std::setw(6) << int(opp.win_rate_vs * 100) << "% WR"
          << " | " << std::setw(8) << int(opp.bb_per_100_vs) << " BB/100"
          << " | Net: $" << int(opp.net_vs) << "\n";
      shown++;
    }
    oss << "\n";
  }

  return oss.str();
}

std::string DatabaseManager::GetLeaderboardJSON(int limit) {
  auto& pr = *player_repo_;
  auto leaderboard = pr.GetLeaderboard(limit);

  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < leaderboard.size(); i++) {
    auto& [info, stats] = leaderboard[i];
    if (i > 0) oss << ",";
    oss << "{";
    oss << "\"rank\":" << (i + 1);
    oss << ",\"player_id\":" << info.player_id;
    oss << ",\"name\":\"" << (info.display_name.empty() ? info.name : info.display_name) << "\"";
    oss << ",\"hands_played\":" << info.hands_played;
    oss << ",\"hands_won\":" << info.hands_won;
    oss << ",\"total_net\":" << std::fixed << std::setprecision(0) << info.total_net;
    oss << "}";
  }
  oss << "]";
  return oss.str();
}

std::string DatabaseManager::GetDatabaseStats() {
  auto ov = stat_repo_->GetOverviewStats();
  auto sessions = stat_repo_->GetSessionStats();
  int player_count = static_cast<int>(player_repo_->GetAllPlayers().size());

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "=== Database Statistics ===\n\n";
  oss << "Tables:\n";
  oss << "  Players:     " << player_count << "\n";
  oss << "  Hands:       " << ov.total_hands << "\n";
  oss << "  Sessions:    " << ov.total_sessions << "\n\n";
  oss << "Financial:\n";
  oss << "  Total Buy-in:  $" << int(ov.total_buy_in) << "\n";
  oss << "  Total Cashout: $" << int(ov.total_cash_out) << "\n";
  oss << "  ROI:           " << ov.roi << "%\n\n";
  oss << "Performance:\n";
  oss << "  Win Rate:     " << ov.win_rate * 100 << "%\n";
  oss << "  BB/100:       " << ov.avg_bb_per_100 << "\n";
  oss << "  Avg Net/Hand: $" << ov.avg_net_per_hand << "\n";

  return oss.str();
}

bool DatabaseManager::Backup(const std::string& backup_path) {
  if (!db_ || !db_->IsOpen()) return false;

  sqlite3* backup_db = nullptr;
  int rc = sqlite3_open(backup_path.c_str(), &backup_db);
  if (rc != SQLITE_OK) return false;

  sqlite3_backup* backup = sqlite3_backup_init(backup_db, "main", db_->GetHandle(), "main");
  if (backup) {
    sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
  }

  rc = sqlite3_errcode(backup_db);
  sqlite3_close(backup_db);
  return rc == SQLITE_OK;
}

}  // namespace poker_engine::phase14
