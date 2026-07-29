#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace poker_engine::cfr {

// ==================== 紧凑 CFR 节点（64 字节缓存行对齐） ====================
// 内存优化：每个节点精确占用 64 字节（一个缓存行）
// 支持 ~1000 万节点仅需 640MB 内存

struct alignas(64) CompactCFRNode {
  // ========== 布局 (总计 64 字节) ==========
  // [0-39]    5× int16 = 量化遗憾值 (每个 2 字节)
  // [40-47]   visit_count (uint32) + flags (uint8) + padding
  // [48-63]   4× int4 = 量化策略

  static constexpr int kActions = 5;  // fold, call, half, pot, all_in

  // 遗憾值量化: 范围 [-32768, 32767] → 实际 * 100
  static constexpr double REGRET_SCALE = 100.0;

  // 策略量化: 4-bit 每个 action, 5 actions = 20 bits (放入 3 字节)
  static constexpr uint32_t STRATEGY_BITS = 4;

  int16_t regret_q[kActions];                                 // 量化遗憾值
  uint32_t visit_count : 24;                                  // 访问次数 (16M 次够用)
  uint32_t flags : 8;                                         // 标志位
  uint16_t strategy_q[(kActions * STRATEGY_BITS + 15) / 16];  // 量化策略

  CompactCFRNode() { std::memset(this, 0, sizeof(*this)); }

  // ========== 遗憾值操作 ==========

  double GetRegret(int action) const {
    return static_cast<double>(regret_q[action]) / REGRET_SCALE;
  }

  void SetRegret(int action, double value) {
    // 量化并裁剪
    int16_t q = static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, value * REGRET_SCALE)));
    regret_q[action] = q;
  }

  void AddRegret(int action, double delta) {
    double current = GetRegret(action);
    SetRegret(action, current + delta);
  }

  // ========== 策略操作 ==========

  void SetStrategy(int action, double prob) {
    // 4-bit 量化 (0-15)
    uint32_t q = static_cast<uint32_t>(std::max(0.0, std::min(15.0, prob * 15.0)));

    int bit_pos = action * STRATEGY_BITS;
    int idx = bit_pos / 16;
    int offset = bit_pos % 16;

    uint32_t mask = 0xF << offset;
    strategy_q[idx] = (strategy_q[idx] & ~mask) | (q << offset);
  }

  double GetStrategy(int action) const {
    int bit_pos = action * STRATEGY_BITS;
    int idx = bit_pos / 16;
    int offset = bit_pos % 16;

    uint32_t mask = 0xF << offset;
    uint32_t q = (strategy_q[idx] & mask) >> offset;

    return static_cast<double>(q) / 15.0;
  }

  // ========== 统计 ==========

  void IncrementVisits() {
    if (visit_count < 0xFFFFFF) visit_count++;
  }

  uint32_t GetVisits() const { return visit_count; }
};

// ==================== 紧凑节点存储 ====================

class CompactNodeStore {
 public:
  CompactNodeStore(size_t max_nodes = 10'000'000) : next_slot_(0), nodes_(max_nodes) {}

  CompactCFRNode* GetOrCreate(uint64_t key) {
    auto it = key_index_.find(key);
    if (it != key_index_.end()) return &nodes_[it->second];
    if (next_slot_ >= nodes_.size()) return nullptr;
    size_t pos = next_slot_++;
    nodes_[pos].flags |= 0x1;
    key_index_[key] = pos;
    return &nodes_[pos];
  }

  CompactCFRNode* Get(uint64_t key) {
    auto it = key_index_.find(key);
    return it != key_index_.end() ? &nodes_[it->second] : nullptr;
  }

  size_t Capacity() const { return nodes_.size(); }
  size_t MemoryBytes() const {
    return nodes_.size() * sizeof(CompactCFRNode) + key_index_.size() * 24;
  }

 private:
  size_t next_slot_;
  std::vector<CompactCFRNode> nodes_;
  std::unordered_map<uint64_t, size_t> key_index_;
};

}  // namespace poker_engine::cfr
