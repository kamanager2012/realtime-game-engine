#include "poker_engine/economy/wallet.h"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

using namespace poker_engine::economy;

class WalletTest : public ::testing::Test {
 protected:
  void SetUp() override { wallet_ = std::make_unique<Wallet>(100, 1000); }
  std::unique_ptr<Wallet> wallet_;
};

TEST_F(WalletTest, InitialBalance) { EXPECT_EQ(wallet_->Balance(), 1000); }

TEST_F(WalletTest, AddFunds) {
  EXPECT_TRUE(wallet_->Add(500));
  EXPECT_EQ(wallet_->Balance(), 1500);
}

TEST_F(WalletTest, AddNegativeFails) {
  EXPECT_FALSE(wallet_->Add(-100));
  EXPECT_EQ(wallet_->Balance(), 1000);
}

TEST_F(WalletTest, SubtractFunds) {
  EXPECT_TRUE(wallet_->Subtract(300));
  EXPECT_EQ(wallet_->Balance(), 700);
}

TEST_F(WalletTest, SubtractMoreThanBalanceFails) {
  EXPECT_FALSE(wallet_->Subtract(2000));
  EXPECT_EQ(wallet_->Balance(), 1000);
}

TEST_F(WalletTest, CanAfford) {
  EXPECT_TRUE(wallet_->CanAfford(500));
  EXPECT_TRUE(wallet_->CanAfford(1000));
  EXPECT_FALSE(wallet_->CanAfford(1001));
}

TEST_F(WalletTest, TransactionHistory) {
  wallet_->Add(200);
  wallet_->Subtract(50);

  auto history = wallet_->History(10);
  EXPECT_EQ(history.size(), 2u);
  EXPECT_EQ(history[0].amount, 200);
  EXPECT_EQ(history[1].amount, -50);
}

TEST_F(WalletTest, HistoryLimited) {
  for (int i = 0; i < 100; ++i) {
    wallet_->Add(10);
  }

  auto history = wallet_->History(10);
  EXPECT_EQ(history.size(), 10u);
}

TEST_F(WalletTest, RaceConditionSafety) {
  // 多线程并发测试
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([this]() {
      for (int j = 0; j < 100; ++j) {
        wallet_->Add(1);
        wallet_->Subtract(1);
      }
    });
  }
  for (auto& t : threads) t.join();

  // 最终余额应该不变
  EXPECT_EQ(wallet_->Balance(), 1000);
}
