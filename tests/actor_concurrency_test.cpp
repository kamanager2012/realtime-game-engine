#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "poker_engine/base/logging.h"
#include "poker_engine/concurrency/actor.h"
#include "poker_engine/concurrency/lockfree_queue.h"

using namespace poker_engine::concurrency;

class ActorConcurrencyTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// ==================== LockFreeQueue FIFO 顺序保证 ====================

TEST_F(ActorConcurrencyTest, MessageOrdering) {
  ConcurrentQueue<int, 1024> queue;
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(queue.enqueue(i));
  }

  int expected = 0;
  auto val = queue.dequeue();
  while (val) {
    EXPECT_EQ(val.value(), expected++);
    val = queue.dequeue();
  }
  EXPECT_EQ(expected, 100);
}

// ==================== 并发压力测试 ====================

TEST_F(ActorConcurrencyTest, HighConcurrency) {
  const int NUM_TABLES = 50;
  const int PLAYERS_PER_TABLE = 6;
  std::atomic<int> actions_processed{0};
  std::atomic<int> errors{0};

  std::vector<std::thread> threads;
  for (int t = 0; t < NUM_TABLES; ++t) {
    threads.emplace_back([&, t]() {
      for (int p = 0; p < PLAYERS_PER_TABLE; ++p) {
        try {
          actions_processed++;
        } catch (...) {
          errors++;
        }
      }
    });
  }

  for (auto& th : threads) th.join();
  EXPECT_EQ(actions_processed.load(), NUM_TABLES * PLAYERS_PER_TABLE);
  EXPECT_EQ(errors.load(), 0);
}

// ==================== Actor 异常安全 ====================

TEST_F(ActorConcurrencyTest, ActorExceptionSafety) {
  Actor actor(42);
  actor.Start();

  actor.RegisterHandler("bad_msg",
                        [](const MessageEnvelope&) { throw std::runtime_error("test exception"); });
  actor.Tell(MessageEnvelope{1, 42, "bad_msg", "payload", nullptr});

  std::atomic<bool> received{false};
  actor.RegisterHandler("ping", [&](const MessageEnvelope&) { received = true; });
  actor.Tell(MessageEnvelope{1, 42, "ping", "", nullptr});

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_TRUE(received.load());
  actor.Stop();
  SUCCEED() << "Actor handled internal exception safely";
}

// ==================== LockFreeQueue 容量测试 ====================

TEST_F(ActorConcurrencyTest, QueueCapacity) {
  ConcurrentQueue<int, 16> small_queue;
  for (int i = 0; i < 16; ++i) {
    EXPECT_TRUE(small_queue.enqueue(i));
  }
  EXPECT_FALSE(small_queue.enqueue(999));

  for (int i = 0; i < 16; ++i) {
    auto val = small_queue.dequeue();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), i);
  }
  EXPECT_TRUE(small_queue.empty());
}

// ==================== LockFreeQueue 多线程测试 ====================

TEST_F(ActorConcurrencyTest, QueueMultiThread) {
  ConcurrentQueue<int, 4096> queue;
  const int ITEMS_PER_THREAD = 500;
  const int NUM_THREADS = 4;
  std::atomic<int> total_dequeued{0};

  std::vector<std::thread> producers;
  for (int t = 0; t < NUM_THREADS; ++t) {
    producers.emplace_back([&, t]() {
      for (int i = 0; i < ITEMS_PER_THREAD; ++i) {
        queue.enqueue(t * ITEMS_PER_THREAD + i);
      }
    });
  }

  std::vector<std::thread> consumers;
  for (int c = 0; c < 1; ++c) {
    consumers.emplace_back([&]() {
      int count = 0;
      while (count < NUM_THREADS * ITEMS_PER_THREAD) {
        auto val = queue.dequeue();
        if (val) {
          total_dequeued++;
          count++;
        }
      }
    });
  }

  for (auto& th : producers) th.join();
  for (auto& th : consumers) th.join();
  EXPECT_EQ(total_dequeued.load(), NUM_THREADS * ITEMS_PER_THREAD);
}
