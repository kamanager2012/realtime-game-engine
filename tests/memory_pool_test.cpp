#if 0  // Phase20/21 headers unavailable - disabled until phase20/21 compile fixes
#include <gtest/gtest.h>
#include <pthread.h>
#include <stdlib.h>

#include "poker_engine_memory_pool/MemoryPool.h"

using namespace Disruptor;

class MemoryPoolTest : public ::testing::Test
{
protected:
    MemoryPool<EventData>* pool_;
    static const int POOL_SIZE = 1024;

    void SetUp() override
    {
        pool_ = new MemoryPool<EventData>(POOL_SIZE);
    }

    void TearDown() override
    {
        delete pool_;
    }
};

// ==================== 基本分配/释放测试 ====================

TEST_F(MemoryPoolTest, BasicAllocFree)
{
    uint64_t sequence;
    EventData* data = pool_->Next(sequence);
    EXPECT_NE(data, nullptr);
    EXPECT_GE(sequence, 0);
    pool_->Free(sequence);
}

TEST_F(MemoryPoolTest, MultipleAllocFree)
{
    std::vector<uint64_t> sequences;
    for (int i = 0; i < 10; ++i)
    {
        uint64_t seq;
        EventData* data = pool_->Next(seq);
        EXPECT_NE(data, nullptr);
        sequences.push_back(seq);
    }

    for (auto seq : sequences)
    {
        pool_->Free(seq);
    }
}

// ==================== 池大小限制 ====================

TEST_F(MemoryPoolTest, PoolCapacity)
{
    // 池大小应该限制分配数量
    std::vector<uint64_t> sequences;
    for (int i = 0; i <= POOL_SIZE; ++i)
    {
        uint64_t seq;
        EventData* data = pool_->Next(seq);
        if (i < POOL_SIZE)
        {
            EXPECT_NE(data, nullptr);
            sequences.push_back(seq);
        }
        else
        {
            // POOL_SIZE 之后，不能再分配
            EXPECT_EQ(data, nullptr);
        }
    }

    for (auto seq : sequences)
    {
        pool_->Free(seq);
    }
}

// ==================== 数据完整性验证 ====================

TEST_F(MemoryPoolTest, DataIntegrity)
{
    uint64_t seq1;
    EventData* data1 = pool_->Next(seq1);
    data1->price = 1234.56;
    data1->volume = 789;
    snprintf(data1->symbol, sizeof(data1->symbol), "AAPL");
    data1->bid = 1234.50;
    data1->ask = 1234.60;

    pool_->Free(seq1);

    // 再次分配，应覆盖相同的内存
    uint64_t seq2;
    EventData* data2 = pool_->Next(seq2);
    EXPECT_EQ(seq1, seq2); // Reuse the same slot

    // 重新赋值并验证
    data2->price = 5678.90;
    EXPECT_DOUBLE_EQ(data2->price, 5678.90);

    pool_->Free(seq2);
}

// ==================== 循环使用验证 ====================

TEST_F(MemoryPoolTest, CircularUsage)
{
    // 先用满
    std::vector<uint64_t> seqs;
    for (int i = 0; i < POOL_SIZE; ++i)
    {
        uint64_t seq;
        EventData* data = pool_->Next(seq);
        EXPECT_NE(data, nullptr);
        seqs.push_back(seq);
    }

    // 再释放
    for (auto seq : seqs)
    {
        pool_->Free(seq);
    }

    // 再次使用，应能重新分配
    uint64_t seq;
    EventData* data = pool_->Next(seq);
    EXPECT_NE(data, nullptr);
    pool_->Free(seq);
}

// ==================== 多线程压力测试 ====================

TEST_F(MemoryPoolTest, MultiThreadStressTest)
{
    const int NUM_THREADS = 4;
    const int ITERATIONS = 2500; // 每线程
    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};

    auto worker = [&]()
    {
        for (int i = 0; i < ITERATIONS; ++i)
        {
            uint64_t seq;
            EventData* data = pool_->Next(seq);
            if (data)
            {
                data->price = 100.0 + (rand() / (double)RAND_MAX) * 100;
                data->volume = rand() % 10000;
                snprintf(data->symbol, sizeof(data1->symbol), "TEST%03d", i % 1000);
                data->bid = data->price - 0.05;
                data->ask = data->price + 0.05;
                pool_->Free(seq);
                success_count++;
            }
            else
            {
                fail_count++;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
    {
        threads.emplace_back(worker);
    }

    for (auto& t : threads)
    {
        t.join();
    }

    printf("Success: %d, Fail: %d\n", success_count.load(), fail_count.load());
    EXPECT_GT(success_count.load(), 0);
}

// ==================== 性能基准 ====================

TEST_F(MemoryPoolTest, PoolPerfBenchmark)
{
    const int ITERATIONS = 1000000;

    // 测试 pool 分配性能
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i)
    {
        uint64_t seq;
        pool_->Next(seq);
        pool_->Free(seq);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double pool_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / (double)ITERATIONS;
    LOG_INFO("Pool alloc/free: avg {} ns", pool_ns);
    EXPECT_LT(pool_ns, 200); // Pool should be < 200ns per op
}

TEST_F(MemoryPoolTest, NewDeletePerfBenchmark)
{
    const int ITERATIONS = 1000000;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i)
    {
        EventData* data = new EventData();
        delete data;
    }
    auto end = std::chrono::high_resolution_clock::now();
    double heap_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / (double)ITERATIONS;
    LOG_INFO("new/delete: avg {} ns", heap_ns);
    LOG_INFO("Pool is {:.1f}x faster than new/delete", heap_ns / pool_ns);
}

#endif  // Phase20/21 headers unavailable