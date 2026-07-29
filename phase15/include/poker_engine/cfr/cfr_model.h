#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "cfr_abstraction.h"
#include "cfr_node.h"
#include "types.h"

namespace poker_engine::cfr {

struct ModelHeader {
  char magic[8] = "POKERCF";
  uint32_t version = 1;
  uint64_t node_count = 0;
  uint32_t num_buckets = 169;
  double exploitability = 0.0;
  uint32_t hash_method = 0;
};

struct NodeRecord {
  uint64_t key_hash;
  double regret_sum[CFRNode::kMaxActions];
  double strategy_sum[CFRNode::kMaxActions];
  int64_t times_visited;
};

static_assert(sizeof(NodeRecord) == 8 + 8 * 5 + 8 * 5 + 8, "NodeRecord size check");

class CFRModelIO {
 public:
  static bool Save(const std::string& filepath, const std::unordered_map<uint64_t, CFRNode>& nodes,
                   double exploitability = 0.0);

  static bool Load(const std::string& filepath, std::unordered_map<uint64_t, CFRNode>& nodes_out);

  static bool SaveCompact(const std::string& filepath,
                          const std::unordered_map<uint64_t, CFRNode>& nodes,
                          double min_strategy_threshold = 0.01);

  struct FileInfo {
    uint64_t node_count;
    uint32_t version;
    double exploitability;
  };
  static std::optional<FileInfo> GetInfo(const std::string& filepath);
};

class ModelCompressor {
 public:
  static std::vector<uint8_t> CompressNodes(const std::vector<NodeRecord>& nodes);

  static std::vector<NodeRecord> DecompressNodes(const std::vector<uint8_t>& data);

 private:
  static void WriteVarInt(std::vector<uint8_t>& out, uint64_t value);
  static uint64_t ReadVarInt(const uint8_t*& data, const uint8_t* end);
};

}  // namespace poker_engine::cfr
