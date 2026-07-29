#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#include "poker_engine/persistence/async_writer.h"
#include "poker_engine/persistence/database_manager.h"
#include "poker_engine/persistence/player_repository.h"

using namespace poker_engine;
using namespace poker_engine::persistence;

class DBConcurrencyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_db_path_ = "/tmp/poker_concurrency_test_" + std::to_string(::getpid()) + ".db";
    std::filesystem::remove(test_db_path_);

    db_ = std::make_unique<DatabaseManager>(test_db_path_, DBType::SQLite);
    ASSERT_TRUE(db_->Connect());

    player_repo_ = std::make_unique<SQLitePlayerRepository>(*db_);
  }

  void TearDown() override {
    db_->Disconnect();
    std::filesystem::remove(test_db_path_);
  }

  std::string test_db_path_;
  std::unique_ptr<DatabaseManager> db_;
  std::unique_ptr<SQLitePlayerRepository> player_repo_;
};

TEST_F(DBConcurrencyTest, ConcurrentPlayerCreation) {
  constexpr int kThreads = 10;
  constexpr int kPerThread = 100;
  std::atomic<int> success_count{0};
  std::atomic<int> fail_count{0};

  auto worker = [&](int thread_id) {
    for (int i = 0; i < kPerThread; ++i) {
      std::string username = "user_t" + std::to_string(thread_id) + "_" + std::to_string(i);
      int64_t id = player_repo_->Create(username, username, "hash");
      if (id > 0)
        success_count++;
      else
        fail_count++;
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back(worker, i);
  }
  for (auto& t : threads) t.join();

  std::cout << "\nCreated " << success_count << " players (" << fail_count << " failed)"
            << std::endl;

  EXPECT_EQ(success_count, kThreads * kPerThread);
}

TEST_F(DBConcurrencyTest, ConcurrentReadWrite) {
  int64_t pid = player_repo_->Create("rw_player", "RW", "hash");
  std::atomic<bool> stop{false};
  std::atomic<int64_t> total_reads{0};
  std::atomic<int> read_failures{0};

  std::thread writer([&]() {
    for (int i = 0; i < 500 && !stop.load(); ++i) {
      player_repo_->UpdateChips(pid, 100 + i);
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });

  std::thread reader([&]() {
    for (int i = 0; i < 500 && !stop.load(); ++i) {
      auto p = player_repo_->GetById(pid);
      if (p.has_value()) {
        total_reads++;
      } else {
        read_failures++;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });

  std::this_thread::sleep_for(std::chrono::seconds(2));
  stop.store(true);
  writer.join();
  reader.join();

  std::cout << "\nConcurrent reads: " << total_reads.load() << " failures: " << read_failures.load()
            << std::endl;

  EXPECT_GT(total_reads.load(), 0);
}
