#include "poker_engine/cfr/cfr_model.h"

#include <fmt/format.h>

#include <algorithm>
#include <cstring>

#include "poker_engine/base/logging.h"

namespace poker_engine::cfr {

bool CFRModelIO::Save(const std::string& filepath,
                      const std::unordered_map<uint64_t, CFRNode>& nodes, double exploitability) {
  std::ofstream ofs(filepath, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) {
    PE_LOG_ERROR("Failed to open {} for writing", filepath);
    return false;
  }

  ModelHeader header;
  header.node_count = nodes.size();
  header.exploitability = exploitability;
  ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));

  for (auto& [key, node] : nodes) {
    NodeRecord record;
    record.key_hash = key;
    memcpy(record.regret_sum, node.regret_sum, sizeof(record.regret_sum));
    memcpy(record.strategy_sum, node.strategy_sum, sizeof(record.strategy_sum));
    record.times_visited = node.times_visited;
    ofs.write(reinterpret_cast<const char*>(&record), sizeof(record));
  }

  if (!ofs.good()) {
    PE_LOG_ERROR("Write error when saving CFR model to {}", filepath);
    return false;
  }

  ofs.close();
  PE_LOG_INFO("CFR model saved: {} nodes to {}", nodes.size(), filepath);
  return true;
}

bool CFRModelIO::Load(const std::string& filepath,
                      std::unordered_map<uint64_t, CFRNode>& nodes_out) {
  std::ifstream ifs(filepath, std::ios::binary);
  if (!ifs.is_open()) {
    PE_LOG_ERROR("Failed to open {} for reading", filepath);
    return false;
  }

  ModelHeader header;
  ifs.read(reinterpret_cast<char*>(&header), sizeof(header));

  if (strncmp(header.magic, "POKERCF", 7) != 0) {
    PE_LOG_ERROR("Invalid model file magic: {}", std::string(header.magic, 7));
    return false;
  }

  if (header.version > 1) {
    PE_LOG_WARN("Model version {} may not be compatible (expected 1)", header.version);
  }

  nodes_out.clear();
  nodes_out.reserve(header.node_count);

  for (uint64_t i = 0; i < header.node_count; ++i) {
    NodeRecord record;
    ifs.read(reinterpret_cast<char*>(&record), sizeof(record));

    CFRNode node;
    memcpy(node.regret_sum, record.regret_sum, sizeof(node.regret_sum));
    memcpy(node.strategy_sum, record.strategy_sum, sizeof(node.strategy_sum));
    node.times_visited = record.times_visited;
    node.compute_strategy();

    nodes_out[record.key_hash] = node;
  }

  ifs.close();
  PE_LOG_INFO("CFR model loaded: {} nodes, exploitability={}", nodes_out.size(),
              header.exploitability);
  return true;
}

bool CFRModelIO::SaveCompact(const std::string& filepath,
                             const std::unordered_map<uint64_t, CFRNode>& nodes,
                             double min_strategy_threshold) {
  std::vector<std::pair<uint64_t, CFRNode>> filtered;

  for (auto& [key, node] : nodes) {
    if (node.times_visited > 0) {
      double avg[CFRNode::kMaxActions];
      node.get_average_strategy(avg);

      bool significant = false;
      for (int a = 0; a < CFRNode::kMaxActions; ++a) {
        if (avg[a] > min_strategy_threshold) {
          significant = true;
          break;
        }
      }

      if (significant) filtered.push_back({key, node});
    }
  }

  PE_LOG_INFO("Compact model: {} / {} nodes significant (threshold={}", filtered.size(),
              nodes.size(), min_strategy_threshold);

  return true;
}

std::optional<typename CFRModelIO::FileInfo> CFRModelIO::GetInfo(const std::string& filepath) {
  std::ifstream ifs(filepath, std::ios::binary);
  if (!ifs.is_open()) return std::nullopt;

  FileInfo info{};
  ModelHeader header;
  ifs.read(reinterpret_cast<char*>(&header), sizeof(header));

  if (strncmp(header.magic, "POKERCF", 7) != 0) return std::nullopt;

  info.version = header.version;
  info.node_count = header.node_count;
  info.exploitability = header.exploitability;
  return info;
}

// ==================== ModelCompressor ====================

void ModelCompressor::WriteVarInt(std::vector<uint8_t>& out, uint64_t value) {
  while (value > 0x7F) {
    out.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
    value >>= 7;
  }
  out.push_back(static_cast<uint8_t>(value));
}

uint64_t ModelCompressor::ReadVarInt(const uint8_t*& data, const uint8_t* end) {
  uint64_t result = 0;
  int shift = 0;
  while (data < end) {
    uint8_t b = *data++;
    result |= static_cast<uint64_t>(b & 0x7F) << shift;
    if ((b & 0x80) == 0) break;
    shift += 7;
  }
  return result;
}

std::vector<uint8_t> ModelCompressor::CompressNodes(const std::vector<NodeRecord>& nodes) {
  std::vector<uint8_t> buffer;
  WriteVarInt(buffer, nodes.size());

  uint64_t prev_key = 0;
  for (auto& node : nodes) {
    WriteVarInt(buffer, node.key_hash - prev_key);
    prev_key = node.key_hash;

    for (int i = 0; i < CFRNode::kMaxActions; ++i) {
      int64_t val = static_cast<int64_t>(node.regret_sum[i] * 1000.0);
      WriteVarInt(buffer, static_cast<uint64_t>((val << 1) ^ (val >> 63)));
    }
    for (int i = 0; i < CFRNode::kMaxActions; ++i) {
      int64_t val = static_cast<int64_t>(node.strategy_sum[i] * 1000.0);
      WriteVarInt(buffer, static_cast<uint64_t>((val << 1) ^ (val >> 63)));
    }
  }

  return buffer;
}

std::vector<NodeRecord> ModelCompressor::DecompressNodes(const std::vector<uint8_t>& data) {
  std::vector<NodeRecord> nodes;
  const uint8_t* ptr = data.data();
  const uint8_t* end = data.data() + data.size();

  uint64_t count = ReadVarInt(ptr, end);
  nodes.reserve(count);

  uint64_t prev_key = 0;
  for (uint64_t i = 0; i < count && ptr < end; ++i) {
    NodeRecord record;
    record.key_hash = prev_key + ReadVarInt(ptr, end);
    prev_key = record.key_hash;

    for (int j = 0; j < CFRNode::kMaxActions && ptr < end; ++j) {
      uint64_t encoded = ReadVarInt(ptr, end);
      int64_t val = (encoded >> 1) ^ -(encoded & 1);
      record.regret_sum[j] = val / 1000.0;
    }
    for (int j = 0; j < CFRNode::kMaxActions && ptr < end; ++j) {
      uint64_t encoded = ReadVarInt(ptr, end);
      int64_t val = (encoded >> 1) ^ -(encoded & 1);
      record.strategy_sum[j] = val / 1000.0;
    }

    record.times_visited = 0;
    nodes.push_back(record);
  }

  return nodes;
}

}  // namespace poker_engine::cfr
