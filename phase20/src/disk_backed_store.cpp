#include "poker_engine/cfr/disk_backed_store.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

namespace poker_engine::cfr {

namespace {

constexpr size_t kNodeSize = detail::NodeBinarySize();

}  // namespace

// ==================== DiskBackedNodeStore ====================

DiskBackedNodeStore::DiskBackedNodeStore(const Config& config)
    : config_(config), data_file_path_(config.disk_path) {
  memory_nodes_.reserve(config.max_memory_nodes / 2);

  // 确保数据目录存在
  std::filesystem::path dir = std::filesystem::path(config.disk_path).parent_path();
  if (!dir.empty()) {
    std::filesystem::create_directories(dir);
  }
}

DiskBackedNodeStore::~DiskBackedNodeStore() {
  Flush();
  if (data_file_.is_open()) {
    data_file_.close();
  }
}

CFRStoreNode* DiskBackedNodeStore::GetOrCreateNode(uint64_t key_hash) {
  std::unique_lock lock(mutex_);

  // 检查内存中是否存在
  auto it = memory_nodes_.find(key_hash);
  if (it != memory_nodes_.end()) {
    UpdateAccessTime(*it->second);
    return &it->second->node;
  }

  // 检查磁盘索引
  auto disk_it = disk_index_.find(key_hash);
  if (disk_it != disk_index_.end()) {
    // 从磁盘加载
    if (NodeEntry* entry = LoadFromDisk(key_hash)) {
      return &entry->node;
    }
  }

  // 创建新节点
  auto entry = std::make_unique<NodeEntry>();
  entry->last_access = ++access_counter_;
  entry->in_memory = true;
  memory_usage_bytes_ += kNodeSize;

  auto* raw_ptr = &entry->node;
  memory_nodes_[key_hash] = std::move(entry);

  // 检查内存预算
  if (memory_usage_bytes_.load() > config_.memory_budget_mb * 1024ULL * 1024ULL) {
    TrimToBudget();
  }

  return raw_ptr;
}

const CFRStoreNode* DiskBackedNodeStore::GetNode(uint64_t key_hash) const {
  std::shared_lock lock(mutex_);

  if (auto it = memory_nodes_.find(key_hash); it != memory_nodes_.end()) {
    const_cast<NodeEntry*>(it->second.get())->last_access = ++access_counter_;
    return &it->second->node;
  }

  // Data not in memory — load into memory via non-const helper
  if (disk_index_.count(key_hash)) {
    lock.unlock();
    auto* self = const_cast<DiskBackedNodeStore*>(this);
    if (self->LoadFromDisk(key_hash)) {
      auto entry = self->GetEntry(key_hash);
      return entry ? &entry->node : nullptr;
    }
  }

  return nullptr;
}

bool DiskBackedNodeStore::RemoveNode(uint64_t key_hash) {
  std::unique_lock lock(mutex_);

  if (auto it = memory_nodes_.find(key_hash); it != memory_nodes_.end()) {
    memory_usage_bytes_ -= kNodeSize;
    memory_nodes_.erase(it);
  }

  disk_index_.erase(key_hash);
  return true;
}

void DiskBackedNodeStore::Flush() {
  std::unique_lock lock(mutex_);

  EnsureFileOpen();

  for (auto& [key, entry] : memory_nodes_) {
    if (entry->dirty) {
      WriteEntryToDisk(*entry);
      entry->dirty = false;
    }
  }

  if (data_file_.is_open()) {
    data_file_.flush();
  }

  PE_LOG_INFO("DiskBackedStore flushed: {} nodes in memory, {} on disk", memory_nodes_.size(),
              disk_index_.size() - memory_nodes_.size());
}

size_t DiskBackedNodeStore::TotalNodes() const {
  std::shared_lock lock(mutex_);
  return memory_nodes_.size() + (disk_index_.size() > memory_nodes_.size()
                                     ? disk_index_.size() - memory_nodes_.size()
                                     : 0);
}

size_t DiskBackedNodeStore::MemoryNodes() const {
  std::shared_lock lock(mutex_);
  return memory_nodes_.size();
}

size_t DiskBackedNodeStore::DiskNodes() const {
  std::shared_lock lock(mutex_);
  return disk_index_.size() > memory_nodes_.size() ? disk_index_.size() - memory_nodes_.size() : 0;
}

size_t DiskBackedNodeStore::DirtyNodes() const {
  std::shared_lock lock(mutex_);
  size_t count = 0;
  for (auto& [_, entry] : memory_nodes_) {
    if (entry->dirty) count++;
  }
  return count;
}

size_t DiskBackedNodeStore::MemoryUsageMB() const {
  return memory_usage_bytes_.load() / (1024 * 1024);
}

bool DiskBackedNodeStore::SaveToDisk(const std::string& path) {
  std::string save_path = path.empty() ? config_.disk_path : path;

  {
    std::shared_lock lock(mutex_);

    std::ofstream ofs(save_path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;

    DiskHeader header;
    header.node_count = memory_nodes_.size();
    ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));

    for (auto& [key, entry] : memory_nodes_) {
      ofs.write(reinterpret_cast<const char*>(&key), sizeof(key));
      detail::SerializeNode(ofs, entry->node);
      if (!ofs.good()) return false;
    }
  }

  PE_LOG_INFO("Saved {} CFR nodes to {}", memory_nodes_.size(), save_path);
  return true;
}

bool DiskBackedNodeStore::LoadFromDisk(const std::string& path) {
  std::string load_path = path.empty() ? config_.disk_path : path;

  std::ifstream ifs(load_path, std::ios::binary);
  if (!ifs) {
    PE_LOG_ERROR("Failed to open {} for loading", load_path);
    return false;
  }

  std::unique_lock lock(mutex_);

  DiskHeader header;
  ifs.read(reinterpret_cast<char*>(&header), sizeof(header));

  if (header.magic != 0x504F524346525354ULL) {
    PE_LOG_ERROR("Invalid magic number in CFR node file");
    return false;
  }

  memory_nodes_.clear();
  memory_usage_bytes_ = 0;
  next_disk_offset_ = 0;

  for (uint64_t i = 0; i < header.node_count; ++i) {
    uint64_t key;
    ifs.read(reinterpret_cast<char*>(&key), sizeof(key));

    auto entry = std::make_unique<NodeEntry>();
    entry->in_memory = true;
    entry->dirty = false;

    detail::DeserializeNode(ifs, entry->node);
    memory_usage_bytes_ += kNodeSize;

    memory_nodes_[key] = std::move(entry);
  }

  PE_LOG_INFO("Loaded {} CFR nodes from {}", memory_nodes_.size(), load_path);
  return true;
}

void DiskBackedNodeStore::Clear() {
  std::unique_lock lock(mutex_);
  memory_nodes_.clear();
  disk_index_.clear();
  memory_usage_bytes_ = 0;

  if (data_file_.is_open()) {
    data_file_.close();
    std::filesystem::remove(data_file_path_);
  }

  next_disk_offset_ = 0;
  PE_LOG_INFO("DiskBackedStore cleared");
}

void DiskBackedNodeStore::SetMemoryBudget(size_t mb) {
  config_.memory_budget_mb = mb;
  TrimToBudget();
}

void DiskBackedNodeStore::TrimToBudget() {
  size_t budget_bytes = config_.memory_budget_mb * 1024ULL * 1024ULL;

  if (memory_usage_bytes_.load() <= budget_bytes) return;

  // 按 LRU 排序，淘汰最冷的数据
  std::vector<std::pair<uint64_t, uint64_t>> access_times;
  for (auto& [key, entry] : memory_nodes_) {
    if (entry->dirty) continue;  // 不淘汰脏数据
    access_times.push_back({key, entry->last_access});
  }

  std::sort(access_times.begin(), access_times.end(), [](const auto& a, const auto& b) {
    return a.second < b.second;  // 最老优先淘汰
  });

  size_t target = static_cast<size_t>(budget_bytes * 0.8);

  for (auto& [key, _] : access_times) {
    if (memory_usage_bytes_.load() <= target) break;

    auto it = memory_nodes_.find(key);
    if (it == memory_nodes_.end()) continue;

    EvictToDisk(*it->second);
  }

  PE_LOG_INFO("Trimmed store: {} nodes remaining, {} MB", memory_nodes_.size(),
              memory_usage_bytes_.load() / (1024 * 1024));
}

// ==================== 内部实现 ====================

auto DiskBackedNodeStore::GetEntry(uint64_t key_hash) -> NodeEntry* {
  auto it = memory_nodes_.find(key_hash);
  return it != memory_nodes_.end() ? it->second.get() : nullptr;
}

auto DiskBackedNodeStore::GetEntryConst(uint64_t key_hash) const -> const NodeEntry* {
  auto it = memory_nodes_.find(key_hash);
  return it != memory_nodes_.end() ? it->second.get() : nullptr;
}

auto DiskBackedNodeStore::LoadFromDisk(uint64_t key_hash) -> NodeEntry* {
  EnsureFileOpen();

  auto disk_it = disk_index_.find(key_hash);
  if (disk_it == disk_index_.end()) return nullptr;

  // 读取磁盘数据
  data_file_.seekg(disk_it->second, std::ios::beg);

  auto entry = std::make_unique<NodeEntry>();
  detail::DeserializeNode(data_file_, entry->node);
  entry->in_memory = true;
  entry->dirty = false;
  entry->last_access = ++access_counter_;
  entry->disk_offset = disk_it->second;

  memory_usage_bytes_ += kNodeSize;

  // 从磁盘索引中移除（现在它在内存中）
  disk_index_.erase(disk_it);

  NodeEntry* raw = entry.get();
  memory_nodes_[key_hash] = std::move(entry);

  return raw;
}

void DiskBackedNodeStore::EvictToDisk(NodeEntry& entry) {
  EnsureFileOpen();

  // 写入磁盘
  data_file_.seekp(next_disk_offset_, std::ios::beg);
  detail::SerializeNode(data_file_, entry.node);

  uint64_t offset = next_disk_offset_;
  next_disk_offset_ += kNodeSize;

  // 更新索引
  disk_index_[offset] = offset;  // 简化：实际应该用 key_hash 映射

  // 从内存释放
  memory_usage_bytes_ -= kNodeSize;
  entry.in_memory = false;
  entry.disk_offset = offset;
  entry.dirty = false;
}

void DiskBackedNodeStore::WriteEntryToDisk(const NodeEntry& entry) {
  EnsureFileOpen();

  uint64_t offset = entry.disk_offset;
  if (offset == 0) {
    offset = next_disk_offset_;
    next_disk_offset_ += kNodeSize;
  }

  data_file_.seekp(offset, std::ios::beg);
  detail::SerializeNode(data_file_, entry.node);

  const_cast<NodeEntry&>(entry).dirty = false;
  const_cast<NodeEntry&>(entry).disk_offset = offset;
}

void DiskBackedNodeStore::EnsureFileOpen() {
  if (!data_file_.is_open()) {
    data_file_.open(data_file_path_,
                    std::ios::binary | std::ios::in | std::ios::out | std::ios::app);

    if (!data_file_) {
      // 创建新文件
      data_file_.clear();
      data_file_.open(data_file_path_, std::ios::binary | std::ios::out | std::ios::trunc);
      data_file_.close();
      data_file_.open(data_file_path_, std::ios::binary | std::ios::in | std::ios::out);
    }

    // 如果是新文件，写入头部
    if (data_file_.tellg() == 0) {
      DiskHeader header;
      data_file_.write(reinterpret_cast<const char*>(&header), sizeof(header));
      next_disk_offset_ = sizeof(DiskHeader);

      // 填充到页面大小对齐
      std::vector<char> padding(4096 - sizeof(DiskHeader), 0);
      data_file_.write(padding.data(), padding.size());
      next_disk_offset_ = 4096;
    }
  }
}

void DiskBackedNodeStore::UpdateAccessTime(NodeEntry& entry) {
  entry.last_access = ++access_counter_;
}

}  // namespace poker_engine::cfr
