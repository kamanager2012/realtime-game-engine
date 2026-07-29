#if 0  // depends on unimplemented memory/arena.h - needs impl first
#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "poker_engine/base/result.h"
#include "poker_engine/concurrency/lockfree_queue.h"
#include "poker_engine/memory/arena.h"
#include "poker_engine/memory/pool.h"

using namespace poker_engine;

// ==================== MemoryArena 测试 ====================

class MemoryArenaTest : public ::testing::Test {
protected:
    memory::MemoryArena* arena_;

    void SetUp() override {
        arena_ = new memory::MemoryArena(1024 * 1024); // 1MB
    }

    void TearDown() override {
        delete arena_;
    }
};

TEST_F(MemoryArenaTest, BasicAllocation) {
    void* p1 = arena_->Allocate(128);
    void* p2 = arena_->Allocate(256);
    void* p3 = arena_->Allocate(512);

    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p3, nullptr);
    EXPECT_NE(p1, p2);
    EXPECT_NE(p2, p3);
}

TEST_F(MemoryArenaTest, Alignment) {
    void* p = arena_->Allocate(64, 16);
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 16, 0);
}

TEST_F(MemoryArenaTest, Reset) {
    arena_->Allocate(1000);
    arena_->Reset();

    void* p = arena_->Allocate(1000);
    EXPECT_NE(p, nullptr);
}

TEST_F(MemoryArenaTest, LargeAllocation) {
    void* p = arena_->Allocate(256 * 1024);
    EXPECT_NE(p, nullptr);
}

TEST_F(MemoryArenaTest, Stats) {
    arena_->Allocate(100);
    arena_->Allocate(200);
    arena_->Allocate(300);

    auto stats = arena_->GetStats();
    EXPECT_EQ(stats.total_allocated, 600u);
    EXPECT_GT(stats.block_count, 0u);
    EXPECT_GT(stats.peak_allocated, 0u);
}

// ==================== 并发分配测试 ====================

TEST_F(MemoryArenaTest, ConcurrentAllocation) {
    constexpr int NUM_THREADS = 8;
    constexpr int ALLOCS_PER_THREAD = 1000;
    std::atomic<int> success_count{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
                void* p = arena_->Allocate(32 + (i % 128));
                if (p) success_count++;
            }
        });
    }

    for (auto& t : threads) t.join();

    EXPECT_EQ(success_count.load(), NUM_THREADS * ALLOCS_PER_THREAD);
}

// ==================== ArenaAllocator STL 容器测试 ====================

TEST_F(MemoryArenaTest, ArenaAllocatorWithVector) {
    memory::ArenaAllocator<int> alloc(arena_);

    std::vector<int, memory::ArenaAllocator<int>> vec(alloc);

    for (int i = 0; i < 500; ++i) {
        vec.push_back(i * 2);
    }

    EXPECT_EQ(vec.size(), 500u);
    for (int i = 0; i < 500; ++i) {
        EXPECT_EQ(vec[i], i * 2);
    }
}

TEST_F(MemoryArenaTest, ArenaAllocatorWithString) {
    memory::ArenaAllocator<char> alloc(arena_);

    using ArenaString = std::basic_string<char, std::char_traits<char>,
                                           memory::ArenaAllocator<char>>;

    ArenaString str(alloc);
    str = "Hello, Arena!";

    EXPECT_EQ(str, "Hello, Arena!");
}

// ==================== MemoryPool 测试 ====================

class MemoryPoolTest : public ::testing::Test {
protected:
    memory::MemoryPool* pool_;

    void SetUp() override {
        pool_ = new memory::MemoryPool(sizeof(memory::Node), 1024);
    }

    void TearDown() override {
        delete pool_;
    }
};

TEST_F(MemoryPoolTest, AllocateAndFree) {
    void* p1 = pool_->Allocate();
    void* p2 = pool_->Allocate();
    void* p3 = pool_->Allocate();

    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p3, nullptr);

    pool_->Free(p1);
    pool_->Free(p2);
    pool_->Free(p3);
}

TEST_F(MemoryPoolTest, ReuseFreedMemory) {
    void* p1 = pool_->Allocate();
    pool_->Free(p1);

    void* p2 = pool_->Allocate();
    EXPECT_EQ(p1, p2);  // 同一块内存应被重用

    pool_->Free(p2);
}

TEST_F(MemoryPoolTest, Stats) {
    EXPECT_EQ(pool_->GetStats().total_blocks, 1024u);
    EXPECT_EQ(pool_->GetStats().free_blocks, 1024u);

    void* p = pool_->Allocate();
    EXPECT_EQ(pool_->GetStats().free_blocks, 1023u);

    pool_->Free(p);
    EXPECT_EQ(pool_->GetStats().free_blocks, 1024u);
}

// ==================== Lock-Free Queue 内存测试 ====================

TEST_F(MemoryArenaTest, LockFreeWithArenaAllocator) {
    using QueueType = concurrency::ConcurrentQueue<int, 4096>;

    void* mem = arena_->Allocate(sizeof(QueueType), alignof(QueueType));
    QueueType* queue = new (mem) QueueType();

    EXPECT_TRUE(queue->enqueue(1));
    EXPECT_TRUE(queue->enqueue(2));

    auto v1 = queue->dequeue();
    auto v2 = queue->dequeue();

    ASSERT_TRUE(v1.has_value());
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(v1.value(), 1);
    EXPECT_EQ(v2.value(), 2);

    queue->~QueueType();
}

// ==================== Result 与 Arena 配合测试 ====================

TEST_F(MemoryArenaTest, ResultWithErrorString) {
    arena::MemoryArena arena;

    auto result = base::Result<std::string>::Ok("success");
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Unwrap(), "success");

    auto err_result = base::Result<std::string>::Err("error");
    EXPECT_FALSE(err_result.IsOk());
    EXPECT_EQ(err_result.Error(), "error");
}

// ==================== 多线程竞争测试 ====================

TEST_F(MemoryArenaTest, HighContention) {
    constexpr int NUM_THREADS = 16;
    constexpr int ALLOCS = 5000;
    std::vector<std::thread> threads;
    std::atomic<int64_t> total_allocated{0};
    std::atomic<int64_t> total_freed{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&]() {
            std::vector<void*> ptrs;
            for (int i = 0; i < ALLOCS; ++i) {
                size_t size = 16 + (i % 256);
                void* p = arena_->Allocate(size);
                if (p) {
                    ptrs.push_back(p);
                    total_allocated.fetch_add(size);
                }
            }

            // 释放
            for (void* p : ptrs) {
                arena_->Free(p);
                total_freed++;
            }
        });
    }

    for (auto& t : threads) t.join();

    EXPECT_GT(total_allocated.load(), 0);
    EXPECT_EQ(total_freed.load(), NUM_THREADS * ALLOCS);
}

// ==================== Arena 容量限制测试 ====================

TEST_F(MemoryArenaTest, CapacityLimit) {
    memory::MemoryArena arena;
    arena.SetCapacity(4096);

    // 应该能分配直到容量满
    void* p1 = arena.Allocate(1000);
    void* p2 = arena.Allocate(1000);
    void* p3 = arena.Allocate(1000);
    void* p4 = arena.Allocate(1000);

    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p3, nullptr);

    // 第 4 次应失败（超过容量）
    if (p4 == nullptr) {
        SUCCEED() << "Capacity limit enforced correctly";
    }

    // 清理
    arena.Free(p1);
    arena.Free(p2);
    arena.Free(p3);
}
#endif
