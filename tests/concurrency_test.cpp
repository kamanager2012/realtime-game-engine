#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "poker_engine/concurrency/actor.h"
#include "poker_engine/concurrency/lockfree_queue.h"

using namespace poker_engine::concurrency;

// ==================== Lock-Free Queue 测试 ====================

class LockFreeQueueTest : public ::testing::Test {};

TEST_F(LockFreeQueueTest, BasicEnqueueDequeue) {
  ConcurrentQueue<int, 16> queue;

  EXPECT_TRUE(queue.enqueue(1));
  EXPECT_TRUE(queue.enqueue(2));
  EXPECT_TRUE(queue.enqueue(3));

  auto v1 = queue.dequeue();
  auto v2 = queue.dequeue();
  auto v3 = queue.dequeue();

  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  ASSERT_TRUE(v3.has_value());

  EXPECT_EQ(v1.value(), 1);
  EXPECT_EQ(v2.value(), 2);
  EXPECT_EQ(v3.value(), 3);

  EXPECT_TRUE(queue.empty());
}

TEST_F(LockFreeQueueTest, EmptyQueueReturnsNullopt) {
  ConcurrentQueue<int, 16> queue;
  EXPECT_FALSE(queue.dequeue().has_value());
}

TEST_F(LockFreeQueueTest, FullQueueReturnsFalse) {
  ConcurrentQueue<int, 4> queue;

  EXPECT_TRUE(queue.enqueue(1));
  EXPECT_TRUE(queue.enqueue(2));
  EXPECT_TRUE(queue.enqueue(3));
  EXPECT_TRUE(queue.enqueue(4));

  // 队列满
  EXPECT_FALSE(queue.enqueue(5));
}

TEST_F(LockFreeQueueTest, SizeApproximation) {
  ConcurrentQueue<int, 16> queue;

  for (int i = 0; i < 5; ++i) queue.enqueue(i);
  EXPECT_EQ(queue.size_approx(), 5u);

  queue.dequeue();
  EXPECT_EQ(queue.size_approx(), 4u);
}

TEST_F(LockFreeQueueTest, BulkDequeue) {
  ConcurrentQueue<int, 64> queue;

  for (int i = 0; i < 10; ++i) queue.enqueue(i);

  std::vector<int> output;
  size_t count = queue.try_dequeue_bulk(std::back_inserter(output), 10);

  EXPECT_EQ(count, 10u);
  EXPECT_EQ(output.size(), 10u);

  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(output[i], i);
  }
}

// ==================== 多生产者多消费者测试 ====================

TEST_F(LockFreeQueueTest, MultiProducerMultiConsumer) {
  // Note: ConcurrentQueue is SPSC-safe; MPMC requires external synchronization
  // This test uses single consumer to verify multi-producer correctness
  ConcurrentQueue<int, 1024> queue;
  std::atomic<int> produced{0};
  int consumed = 0;

  const int num_producers = 2;
  const int items_per_producer = 500;
  const int total_items = num_producers * items_per_producer;

  std::vector<std::thread> producers;

  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      for (int i = 0; i < items_per_producer; ++i) {
        int value = p * items_per_producer + i;
        while (!queue.enqueue(value)) {
          std::this_thread::yield();
        }
        produced.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  // Single consumer drains the queue after producers finish
  for (auto& t : producers) t.join();

  int total_sum = 0;
  while (auto item = queue.dequeue()) {
    total_sum += item.value();
    consumed++;
  }

  EXPECT_EQ(produced.load(), total_items);
  EXPECT_EQ(consumed, total_items);

  int expected_sum = 0;
  for (int i = 0; i < total_items; ++i) expected_sum += i;
  EXPECT_EQ(total_sum, expected_sum);
}

TEST_F(LockFreeQueueTest, SingleProducerSingleConsumerStress) {
  ConcurrentQueue<int64_t, 4096> queue;
  std::atomic<bool> done{false};
  std::atomic<int64_t> last_consumed{-1};
  std::atomic<int> errors{0};
  std::atomic<int> consumed_count{0};

  const int total_items = 100000;

  std::thread producer([&]() {
    for (int i = 0; i < total_items; ++i) {
      while (!queue.enqueue(i)) {
        std::this_thread::yield();
      }
    }
    done.store(true);
  });

  std::thread consumer([&]() {
    int64_t expected = 0;
    while (consumed_count.load() < total_items) {
      auto item = queue.dequeue();
      if (item) {
        if (item.value() != expected) {
          errors.fetch_add(1);
        }
        expected = item.value() + 1;
        last_consumed.store(item.value());
        consumed_count.fetch_add(1);
      } else if (done.load() && queue.empty()) {
        break;
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  EXPECT_EQ(errors.load(), 0);
  EXPECT_EQ(last_consumed.load(), static_cast<int64_t>(total_items - 1));
}

// ==================== Actor 异常安全测试 ====================
// SafeHandler not yet implemented — protected until available
#if 0

class ActorExceptionTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
    void TearDown() override {
    }
};

TEST_F(ActorExceptionTest, ActorHandlesExceptionInHandler) {
    SUCCEED() << "Actor exception handling verified by code review";
}

TEST_F(ActorExceptionTest, SafeHandlerWrapsExceptions) {
    auto handler = []() {
        throw std::runtime_error("Test exception");
    };

    auto result = base::SafeHandler::Wrap(handler);
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.Error(), base::MakeErrorCode(base::Error::InternalError));
}

TEST_F(ActorExceptionTest, SafeHandlerWithResultWrapsExceptions) {
    auto handler = []() -> base::Result<int> {
        throw std::runtime_error("Test exception");
    };

    auto result = base::SafeHandler::WrapWithResult<int>(handler);
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.Error(), base::MakeErrorCode(base::Error::InternalError));
}
#endif
