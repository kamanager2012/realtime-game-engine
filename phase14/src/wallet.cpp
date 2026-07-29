#include "poker_engine/economy/wallet.h"

#include <algorithm>
#include <chrono>
#include <iomanip>

#include "poker_engine/base/logging.h"
#include "poker_engine/persistence/database_manager.h"

namespace poker_engine::economy {

Wallet::Wallet(int64_t player_id, int64_t initial_balance)
    : player_id_(player_id), balance_(initial_balance) {}

int64_t Wallet::Balance() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return balance_;
}

bool Wallet::Add(int64_t amount, Transaction::Type type, const std::string& ref) {
  if (amount <= 0) return false;

  std::lock_guard<std::mutex> lock(mutex_);
  balance_ += amount;

  Transaction txn;
  txn.id = next_txn_id_++;
  txn.player_id = player_id_;
  txn.type = type;
  txn.amount = amount;
  txn.reference = ref;
  txn.balance_after = static_cast<double>(balance_);
  txn.timestamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());

  transactions_.push_back(txn);

  LOG_INFO("Wallet: Player {} +{} (type={}, balance={})", player_id_, amount,
           static_cast<int>(type), balance_);
  return true;
}

bool Wallet::Subtract(int64_t amount, Transaction::Type type, const std::string& ref) {
  if (amount <= 0) return false;

  std::lock_guard<std::mutex> lock(mutex_);
  if (balance_ < amount) return false;

  balance_ -= amount;

  Transaction txn;
  txn.id = next_txn_id_++;
  txn.player_id = player_id_;
  txn.type = type;
  txn.amount = -amount;  // 负数表示扣款
  txn.reference = ref;
  txn.balance_after = static_cast<double>(balance_);
  txn.timestamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());

  transactions_.push_back(txn);

  LOG_INFO("Wallet: Player {} -{} (type={}, balance={})", player_id_, amount,
           static_cast<int>(type), balance_);
  return true;
}

bool Wallet::CanAfford(int64_t amount) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return balance_ >= amount;
}

std::vector<Transaction> Wallet::History(int limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  int start = static_cast<int>(transactions_.size()) - limit;
  if (start < 0) start = 0;
  return std::vector<Transaction>(transactions_.begin() + start, transactions_.end());
}

void Wallet::LoadFromDB() {
  auto& db = persistence::DatabaseManager::Instance();
  db.Query(
      "SELECT COALESCE(SUM(amount), 0) FROM ("
      "  SELECT amount FROM wallet_transactions WHERE player_id = " +
          std::to_string(player_id_) +
          " ORDER BY id DESC LIMIT 1"
          ")",
      [this](const std::vector<std::string>& row) {
        balance_ = static_cast<int64_t>(std::stod(row[0]));
        return false;
      });
  LOG_INFO("Wallet loaded for player {}: balance={}", player_id_, balance_);
}

void Wallet::FlushToDB() {
  // 生产环境应将交易批量写入数据库
  // MVP 阶段暂缓实现
}

}  // namespace poker_engine::economy
