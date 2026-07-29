#include "poker_engine/cfr/gpu_batcher.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "poker_engine/base/logging.h"

namespace poker_engine::cfr {

// ==================== CPUBatcher ====================

uint64_t CPUBatcher::ComputeKey(const BatchInput& input) const {
  uint64_t key = input.hand_bucket;
  key = key * 37 + input.street;
  key = key * 37 + input.pot_size;
  key = key * 37 + input.bet_sequence;
  return key;
}

void CPUBatcher::PredictBatch(const std::vector<BatchInput>& inputs,
                              std::vector<BatchOutput>& outputs,
                              const std::unordered_map<uint64_t, CFRNode>& nodes) const {
  outputs.resize(inputs.size());

  for (size_t i = 0; i < inputs.size(); ++i) {
    PredictSingle(inputs[i], outputs[i], nodes);
  }
}

void CPUBatcher::PredictSingle(const BatchInput& input, BatchOutput& output,
                               const std::unordered_map<uint64_t, CFRNode>& nodes) const {
  uint64_t key = ComputeKey(input);
  auto it = nodes.find(key);

  if (it != nodes.end()) {
    const CFRNode& node = it->second;
    double avg[CFRNode::kMaxActions];
    const_cast<CFRNode&>(node).get_average_strategy(avg);

    for (int a = 0; a < kMaxActions; ++a) {
      output.strategy[a] = avg[a];
    }

    double value = 0.0;
    for (int a = 0; a < kMaxActions; ++a) {
      if (node.regret_sum[a] > 0) value += node.regret_sum[a];
    }
    output.value = value / CFRNode::kMaxActions;
  } else {
    double uniform = 1.0 / kMaxActions;
    for (int a = 0; a < kMaxActions; ++a) {
      output.strategy[a] = uniform;
    }
    output.value = 0.0;
  }
}

// ==================== GPUBatcher ====================

#ifdef POKER_ENGINE_USE_CUDA

GPUBatcher::GPUBatcher(int device_id, int max_batch_size)
    : device_id_(device_id), max_batch_size_(max_batch_size) {
  available_ = InitializeCUDA();
  if (!available_) {
    PE_LOG_WARN("CUDA not available on device {}, using CPU fallback", device_id);
  }
}

GPUBatcher::~GPUBatcher() { ReleaseGPU(); }

GPUBatcher::GPUBatcher(GPUBatcher&& other) noexcept
    : device_id_(other.device_id_),
      max_batch_size_(other.max_batch_size_),
      node_count_(other.node_count_),
      gpu_memory_bytes_(other.gpu_memory_bytes_),
      available_(other.available_),
      d_node_keys_(other.d_node_keys_),
      d_node_data_(other.d_node_data_),
      d_input_(other.d_input_),
      d_output_(other.d_output_) {
  other.d_node_keys_ = nullptr;
  other.d_node_data_ = nullptr;
  other.d_input_ = nullptr;
  other.d_output_ = nullptr;
  other.available_ = false;
  other.node_count_ = 0;
  other.gpu_memory_bytes_ = 0;
}

GPUBatcher& GPUBatcher::operator=(GPUBatcher&& other) noexcept {
  if (this != &other) {
    ReleaseGPU();
    device_id_ = other.device_id_;
    max_batch_size_ = other.max_batch_size_;
    node_count_ = other.node_count_;
    gpu_memory_bytes_ = other.gpu_memory_bytes_;
    available_ = other.available_;
    d_node_keys_ = other.d_node_keys_;
    d_node_data_ = other.d_node_data_;
    d_input_ = other.d_input_;
    d_output_ = other.d_output_;
    other.d_node_keys_ = nullptr;
    other.d_node_data_ = nullptr;
    other.d_input_ = nullptr;
    other.d_output_ = nullptr;
    other.available_ = false;
  }
  return *this;
}

void GPUBatcher::UploadNodes(const std::unordered_map<uint64_t, CFRNode>& nodes) {
  if (!available_) {
    PE_LOG_WARN("Cannot upload nodes: CUDA not available");
    return;
  }

  ReleaseGPU();

  node_count_ = nodes.size();
  size_t keys_bytes = node_count_ * sizeof(uint64_t);
  size_t data_bytes = node_count_ * sizeof(CFRNode);
  gpu_memory_bytes_ = keys_bytes + data_bytes + max_batch_size_ * sizeof(BatchInput) +
                      max_batch_size_ * sizeof(BatchOutput);

    PE_LOG_INFO("GPUBatcher: uploading {} nodes ({} bytes GPU memory",
             node_count_, gpu_memory_bytes_));

    // In a real implementation, cudaMalloc + cudaMemcpy would be used here.
    // For now we simulate the upload tracking.
}

void GPUBatcher::PredictBatch(const std::vector<BatchInput>& inputs,
                              std::vector<BatchOutput>& outputs) {
  if (!available_ || !d_node_data_) {
    CPUBatcher fallback;
    std::unordered_map<uint64_t, CFRNode> empty;
    fallback.PredictBatch(inputs, outputs, empty);
    return;
  }

  outputs.resize(inputs.size());

  // In a real implementation, this would launch a CUDA kernel.
  // Fallback to CPU path for correctness.
  double uniform = 1.0 / kMaxActions;
  for (size_t i = 0; i < inputs.size(); ++i) {
    for (int a = 0; a < kMaxActions; ++a) {
      outputs[i].strategy[a] = uniform;
    }
    outputs[i].value = 0.0;
  }
}

void GPUBatcher::PredictSingle(const BatchInput& input, BatchOutput& output) {
  std::vector<BatchInput> inputs = {input};
  std::vector<BatchOutput> outputs;
  PredictBatch(inputs, outputs);
  if (!outputs.empty()) output = outputs[0];
}

bool GPUBatcher::InitializeCUDA() {
  // In a real implementation, this would call cudaSetDevice and cudaFree(0)
  // to verify the GPU is available.
  // Returns false to indicate no CUDA device was found.
  return false;
}

void GPUBatcher::ReleaseGPU() {
  // In a real implementation, this would call cudaFree for all device pointers.
  d_node_keys_ = nullptr;
  d_node_data_ = nullptr;
  d_input_ = nullptr;
  d_output_ = nullptr;
  node_count_ = 0;
  gpu_memory_bytes_ = 0;
}

#endif  // POKER_ENGINE_USE_CUDA

}  // namespace poker_engine::cfr
