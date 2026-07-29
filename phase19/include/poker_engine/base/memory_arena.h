#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <vector>

namespace poker_engine::base {

class MemoryArena {
 public:
  explicit MemoryArena(size_t initial_size = 1024 * 1024) : block_size_(initial_size) {
    blocks_.reserve(16);
    AddBlock();
  }

  ~MemoryArena() {
    for (auto& block : blocks_) {
      delete[] block.data;
    }
  }

  void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
    std::lock_guard<std::mutex> lock(mutex_);
    return AllocateLocked(size, alignment);
  }

  void* AllocateLocked(size_t size, size_t alignment) {
    size_t space = size + alignment - 1;

    if (current_pos_ + space > block_size_) {
      AddBlock();
    }

    char* ptr = blocks_.back().data + current_pos_;

    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    size_t padding = aligned - addr;

    if (current_pos_ + padding + size > block_size_) {
      AddBlock();
      ptr = blocks_.back().data;
      aligned = reinterpret_cast<uintptr_t>(ptr);
      padding = 0;
    }

    current_pos_ += padding + size;
    total_allocated_ += size;

    return reinterpret_cast<void*>(aligned);
  }

  void Reset() {
    current_pos_ = 0;
    total_allocated_ = 0;
  }

  size_t TotalAllocated() const { return total_allocated_; }
  size_t BlockCount() const { return blocks_.size(); }

  MemoryArena(const MemoryArena&) = delete;
  MemoryArena& operator=(const MemoryArena&) = delete;

 private:
  struct Block {
    char* data;
    size_t size;
  };

  void AddBlock() {
    char* data = new char[block_size_];
    blocks_.push_back({data, block_size_});
    current_pos_ = 0;
    block_size_ = std::min(block_size_ * 2, size_t(64 * 1024 * 1024));
  }

  std::vector<Block> blocks_;
  size_t block_size_;
  size_t current_pos_ = 0;
  size_t total_allocated_ = 0;
  std::mutex mutex_;
};

// ==================== Arena 分配器（STL 兼容） ====================

template <typename T>
class ArenaAllocator {
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

  template <typename U>
  struct rebind {
    using other = ArenaAllocator<U>;
  };

  explicit ArenaAllocator(MemoryArena* arena = nullptr) : arena_(arena ? arena : &default_arena_) {}

  template <typename U>
  ArenaAllocator(const ArenaAllocator<U>& other) : arena_(other.arena_) {}

  pointer allocate(size_type n) {
    return static_cast<pointer>(arena_->Allocate(n * sizeof(T), alignof(T)));
  }

  void deallocate(pointer, size_type) {
    // Arena does not support individual deallocation
  }

  template <typename U>
  bool operator==(const ArenaAllocator<U>& other) const {
    return arena_ == other.arena_;
  }

  template <typename U>
  bool operator!=(const ArenaAllocator<U>& other) const {
    return arena_ != other.arena_;
  }

  MemoryArena* arena_;

 private:
  static MemoryArena default_arena_;
};

template <typename T>
MemoryArena ArenaAllocator<T>::default_arena_;

}  // namespace poker_engine::base
