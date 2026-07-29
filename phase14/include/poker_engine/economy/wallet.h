#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace poker_engine::economy {

struct Transaction {
  enum class Type : uint8_t { BuyIn, Cashout, Win, Bonus, Refund, Fee };

  int64_t id;
  int64_t player_id;
  Type type;
  int64_t amount;
  std::string reference;  // 关联的 hand_id 或其他标识
  std::string note;
  double balance_after;
  std::string timestamp;
};

class Wallet {
 public:
  explicit Wallet(int64_t player_id, int64_t initial_balance = 1000);

  int64_t Balance() const;
  bool Add(int64_t amount, Transaction::Type type = Transaction::Type::Bonus,
           const std::string& ref = "");
  bool Subtract(int64_t amount, Transaction::Type type = Transaction::Type::Fee,
                const std::string& ref = "");
  bool CanAfford(int64_t amount) const;

  std::vector<Transaction> History(int limit = 50) const;

  // 与数据库同步
  void LoadFromDB();
  void FlushToDB();

 private:
  int64_t player_id_;
  int64_t balance_;
  mutable std::mutex mutex_;
  std::vector<Transaction> transactions_;
  int64_t next_txn_id_ = 1;
};

}  // namespace poker_engine::economy
