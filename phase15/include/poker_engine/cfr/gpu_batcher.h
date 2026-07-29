#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "cfr_abstraction.h"
#include "cfr_node.h"
#include "types.h"

namespace poker_engine::cfr {

struct BatchInput {
  uint16_t hand_bucket;
  uint8_t street;
  uint16_t pot_size;
  uint8_t bet_sequence;
};

struct BatchOutput {
  double strategy[kMaxActions];
  double value;
};

class CPUBatcher {
 public:
  CPUBatcher() = default;

  void PredictBatch(const std::vector<BatchInput>& inputs, std::vector<BatchOutput>& outputs,
                    const std::unordered_map<uint64_t, CFRNode>& nodes) const;

  void PredictSingle(const BatchInput& input, BatchOutput& output,
                     const std::unordered_map<uint64_t, CFRNode>& nodes) const;

  size_t MemoryUsage() const { return 0; }
  static const char* Name() { return "CPUBatcher"; }

 private:
  uint64_t ComputeKey(const BatchInput& input) const;
};

#ifdef POKER_ENGINE_USE_CUDA

class GPUBatcher {
 public:
  GPUBatcher(int device_id = 0, int max_batch_size = 4096);
  ~GPUBatcher();

  GPUBatcher(const GPUBatcher&) = delete;
  GPUBatcher& operator=(const GPUBatcher&) = delete;

  GPUBatcher(GPUBatcher&&) noexcept;
  GPUBatcher& operator=(GPUBatcher&&) noexcept;

  void UploadNodes(const std::unordered_map<uint64_t, CFRNode>& nodes);

  void PredictBatch(const std::vector<BatchInput>& inputs, std::vector<BatchOutput>& outputs);

  void PredictSingle(const BatchInput& input, BatchOutput& output);

  int DeviceId() const { return device_id_; }
  int MaxBatchSize() const { return max_batch_size_; }
  size_t NodesOnGPU() const { return node_count_; }
  size_t MemoryUsage() const { return gpu_memory_bytes_; }

  bool IsAvailable() const { return available_; }
  static const char* Name() { return "GPUBatcher(CUDA)"; }

 private:
  int device_id_;
  int max_batch_size_;
  size_t node_count_ = 0;
  size_t gpu_memory_bytes_ = 0;
  bool available_ = false;

  void* d_node_keys_ = nullptr;
  void* d_node_data_ = nullptr;
  void* d_input_ = nullptr;
  void* d_output_ = nullptr;

  bool InitializeCUDA();
  void ReleaseGPU();
};

#else

class GPUBatcher {
 public:
  GPUBatcher(int device_id = 0, int max_batch_size = 4096) : cpu_fallback_() {}

  void UploadNodes(const std::unordered_map<uint64_t, CFRNode>& nodes) { nodes_ = nodes; }

  void PredictBatch(const std::vector<BatchInput>& inputs, std::vector<BatchOutput>& outputs) {
    cpu_fallback_.PredictBatch(inputs, outputs, nodes_);
  }

  void PredictSingle(const BatchInput& input, BatchOutput& output) {
    cpu_fallback_.PredictSingle(input, output, nodes_);
  }

  bool IsAvailable() const { return false; }
  static const char* Name() { return "GPUBatcher(CPU fallback)"; }
  size_t NodesOnGPU() const { return nodes_.size(); }
  size_t MemoryUsage() const { return cpu_fallback_.MemoryUsage(); }

 private:
  CPUBatcher cpu_fallback_;
  std::unordered_map<uint64_t, CFRNode> nodes_;
};

#endif  // POKER_ENGINE_USE_CUDA

}  // namespace poker_engine::cfr
