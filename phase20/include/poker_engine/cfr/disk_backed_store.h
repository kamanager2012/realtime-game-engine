#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "cfr_store_node.h"
#include "poker_engine/base/logging.h"

namespace poker_engine::cfr {

// ==================== 磁盘支持的 CFR 节点存储 ====================
// 当节点数量超过内存限制时自动溢出到磁盘
// 使用 LRU 缓存策略 + 内存映射文件

struct DiskBackedNodeStoreConfig {
  size_t max_memory_nodes = 5000000;
  size_t memory_budget_mb = 2048;
  std::string disk_path = "cfr_nodes.dat";
  int flush_interval_seconds = 60;
  bool use_mmap = true;
};

class DiskBackedNodeStore {
 public:
  using Config = DiskBackedNodeStoreConfig;

  explicit DiskBackedNodeStore(const Config& config = Config());
  ~DiskBackedNodeStore();

  // ========== 节点操作 ==========

  // 获取或创建节点（线程安全）
  CFRStoreNode* GetOrCreateNode(uint64_t key_hash);

  // 获取节点（只读）
  const CFRStoreNode* GetNode(uint64_t key_hash) const;

  // 删除节点（释放内存/磁盘空间）
  bool RemoveNode(uint64_t key_hash);

  // 刷新所有脏节点到磁盘
  void Flush();

  // ========== 统计 ==========

  size_t TotalNodes() const;     // 内存 + 磁盘
  size_t MemoryNodes() const;    // 当前在内存中的节点数
  size_t DiskNodes() const;      // 当前在磁盘上的节点数
  size_t DirtyNodes() const;     // 未刷盘的节点数
  size_t MemoryUsageMB() const;  // 内存使用量

  // ========== 持久化 ==========

  bool SaveToDisk(const std::string& path = "");
  bool LoadFromDisk(const std::string& path = "");

  // 清空所有数据
  void Clear();

  // ========== LRU 管理 ==========

  void SetMemoryBudget(size_t mb);
  void TrimToBudget();  // 淘汰冷数据到磁盘

 private:
  struct NodeEntry {
    CFRStoreNode node;
    mutable bool dirty = false;        // 是否需要写回磁盘
    mutable uint64_t last_access = 0;  // 最近访问时间戳
    bool in_memory = true;             // 当前是否在内存中
    uint64_t disk_offset = 0;          // 磁盘偏移（如果不在内存）
  };

  struct DiskHeader {
    uint64_t magic = 0x504F524346525354ULL;  // "POKERCF" + "ST"
    uint32_t version = 1;
    uint64_t node_count = 0;
    uint64_t node_data_start = 4096;  // 数据区域起始（对齐页面）
  };

  // 内部方法
  NodeEntry* GetEntry(uint64_t key_hash);
  const NodeEntry* GetEntryConst(uint64_t key_hash) const;

  NodeEntry* LoadFromDisk(uint64_t key_hash);
  void EvictToDisk(NodeEntry& entry);
  void WriteEntryToDisk(const NodeEntry& entry);

  void UpdateAccessTime(NodeEntry& entry);

  // 数据文件 I/O
  std::fstream data_file_;
  void EnsureFileOpen();

  void FlushEntry(const NodeEntry& entry, uint64_t offset);
  void ReadEntry(uint64_t offset, NodeEntry& entry) const;

  // 成员变量
  Config config_;

  mutable std::shared_mutex mutex_;
  std::unordered_map<uint64_t, std::unique_ptr<NodeEntry>> memory_nodes_;
  std::unordered_map<uint64_t, uint64_t> disk_index_;  // key_hash → disk_offset

  mutable uint64_t access_counter_ = 0;
  uint64_t next_disk_offset_ = 0;
  std::string data_file_path_;

  // 内存统计
  std::atomic<size_t> memory_usage_bytes_{0};
};

// ==================== 节点序列化 ====================

namespace detail {

inline void SerializeNode(std::ostream& os, const CFRStoreNode& node) {
  os.write(reinterpret_cast<const char*>(node.regret_sum.data()), sizeof(node.regret_sum));
  os.write(reinterpret_cast<const char*>(node.strategy_sum.data()), sizeof(node.strategy_sum));
  os.write(reinterpret_cast<const char*>(node.current_strategy.data()),
           sizeof(node.current_strategy));
  os.write(reinterpret_cast<const char*>(&node.times_visited), sizeof(node.times_visited));
}

inline void DeserializeNode(std::istream& is, CFRStoreNode& node) {
  is.read(reinterpret_cast<char*>(node.regret_sum.data()), sizeof(node.regret_sum));
  is.read(reinterpret_cast<char*>(node.strategy_sum.data()), sizeof(node.strategy_sum));
  is.read(reinterpret_cast<char*>(node.current_strategy.data()), sizeof(node.current_strategy));
  is.read(reinterpret_cast<char*>(&node.times_visited), sizeof(node.times_visited));
}

constexpr size_t NodeBinarySize() {
  return sizeof(double) * CFRStoreNode::kMaxActions * 3 + sizeof(int64_t);
}

}  // namespace detail

}  // namespace poker_engine::cfr
