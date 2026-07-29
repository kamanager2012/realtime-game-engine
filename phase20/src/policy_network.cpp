#include "poker_engine/cfr/policy_network.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <shared_mutex>

#include "poker_engine/base/logging.h"

namespace poker_engine::cfr {

// ==================== PolicyNetwork ====================

PolicyNetwork::PolicyNetwork() { XavierInit(); }

void PolicyNetwork::XavierInit() {
  std::mt19937 rng(42);

  float limit1 = std::sqrt(6.0f / (kInputSize + kHiddenSize));
  std::uniform_real_distribution<float> dist1(-limit1, limit1);
  for (auto& w : w1_) w = dist1(rng);
  for (auto& b : b1_) b = 0.0f;

  float limit2 = std::sqrt(6.0f / (kHiddenSize + kOutputSize));
  std::uniform_real_distribution<float> dist2(-limit2, limit2);
  for (auto& w : w2_) w = dist2(rng);
  for (auto& b : b2_) b = 0.0f;
}

std::array<float, PolicyNetwork::kOutputSize> PolicyNetwork::Forward(const float* input) const {
  std::array<float, kOutputSize> output;

  // Layer 1: Linear + LeakyReLU
  std::array<float, kHiddenSize> hidden;
  for (int h = 0; h < kHiddenSize; ++h) {
    float sum = b1_[h];
    for (int i = 0; i < kInputSize; ++i) {
      sum += input[i] * w1_[i * kHiddenSize + h];
    }
    hidden[h] = LeakyReLU(sum);
  }

  // Layer 2: Linear + Softmax
  for (int o = 0; o < kOutputSize; ++o) {
    float sum = b2_[o];
    for (int h = 0; h < kHiddenSize; ++h) {
      sum += hidden[h] * w2_[h * kOutputSize + o];
    }
    output[o] = sum;
  }

  Softmax(output);
  return output;
}

std::array<float, PolicyNetwork::kOutputSize> PolicyNetwork::Forward(
    const std::array<float, kInputSize>& input) const {
  return Forward(input.data());
}

void PolicyNetwork::ForwardBatch(const float* inputs, float* outputs, int batch_size) const {
  // 循环展开以提高缓存效率
  for (int b = 0; b < batch_size; ++b) {
    const float* input = inputs + b * kInputSize;
    float* output = outputs + b * kOutputSize;

    // Layer 1
    float hidden[kHiddenSize];
    for (int h = 0; h < kHiddenSize; ++h) {
      float sum = b1_[h];
      // SIMD 友好循环
      const float* w_row = &w1_[h];
      for (int i = 0; i < kInputSize; ++i) {
        sum += input[i] * w_row[i * kHiddenSize];
      }
      hidden[h] = LeakyReLU(sum);
    }

    // Layer 2
    for (int o = 0; o < kOutputSize; ++o) {
      float sum = b2_[o];
      for (int h = 0; h < kHiddenSize; ++h) {
        sum += hidden[h] * w2_[h * kOutputSize + o];
      }
      output[o] = sum;
    }

    // Softmax
    Softmax(*reinterpret_cast<std::array<float, kOutputSize>*>(output));
  }
}

void PolicyNetwork::TrainStep(const std::array<float, kInputSize>& input,
                              const std::array<float, kOutputSize>& target, float learning_rate) {
  // 前向传播
  auto output = Forward(input.data());

  // 输出层梯度 (Softmax + Cross-Entropy 的简化梯度)
  float grad2[kOutputSize];
  for (int o = 0; o < kOutputSize; ++o) {
    grad2[o] = output[o] - target[o];
  }

  // 隐藏层梯度
  float grad1[kHiddenSize] = {};
  for (int h = 0; h < kHiddenSize; ++h) {
    float sum = 0;
    for (int o = 0; o < kOutputSize; ++o) {
      sum += grad2[o] * w2_[h * kOutputSize + o];
    }
    // LeakyReLU 导数
    float hidden_val = 0;
    {
      float s = b1_[h];
      for (int i = 0; i < kInputSize; ++i) {
        s += input[i] * w1_[i * kHiddenSize + h];
      }
      hidden_val = LeakyReLU(s);
    }
    grad1[h] = sum * (hidden_val > 0 ? 1.0f : 0.01f);
  }

  // 更新 Layer 2 权重
  float hidden[kHiddenSize];
  for (int h = 0; h < kHiddenSize; ++h) {
    float s = b1_[h];
    for (int i = 0; i < kInputSize; ++i) {
      s += input[i] * w1_[i * kHiddenSize + h];
    }
    hidden[h] = LeakyReLU(s);
  }

  for (int o = 0; o < kOutputSize; ++o) {
    for (int h = 0; h < kHiddenSize; ++h) {
      w2_[h * kOutputSize + o] -= learning_rate * grad2[o] * hidden[h];
    }
    b2_[o] -= learning_rate * grad2[o];
  }

  // 更新 Layer 1 权重
  for (int h = 0; h < kHiddenSize; ++h) {
    for (int i = 0; i < kInputSize; ++i) {
      w1_[i * kHiddenSize + h] -= learning_rate * grad1[h] * input[i];
    }
    b1_[h] -= learning_rate * grad1[h];
  }
}

bool PolicyNetwork::Save(const std::string& path) const {
  std::ofstream ofs(path, std::ios::binary);
  if (!ofs) return false;

  uint32_t magic = 0x504F4C49;  // "POLI"
  ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));

  uint32_t version = 1;
  ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));

  ofs.write(reinterpret_cast<const char*>(w1_.data()), w1_.size() * sizeof(float));
  ofs.write(reinterpret_cast<const char*>(b1_.data()), b1_.size() * sizeof(float));
  ofs.write(reinterpret_cast<const char*>(w2_.data()), w2_.size() * sizeof(float));
  ofs.write(reinterpret_cast<const char*>(b2_.data()), b2_.size() * sizeof(float));

  return ofs.good();
}

bool PolicyNetwork::Load(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) return false;

  uint32_t magic, version;
  ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  if (magic != 0x504F4C49) return false;
  ifs.read(reinterpret_cast<char*>(&version), sizeof(version));

  ifs.read(reinterpret_cast<char*>(w1_.data()), w1_.size() * sizeof(float));
  ifs.read(reinterpret_cast<char*>(b1_.data()), b1_.size() * sizeof(float));
  ifs.read(reinterpret_cast<char*>(w2_.data()), w2_.size() * sizeof(float));
  ifs.read(reinterpret_cast<char*>(b2_.data()), b2_.size() * sizeof(float));

  return ifs.good();
}

void PolicyNetwork::Softmax(std::array<float, kOutputSize>& x) {
  float max_val = *std::max_element(x.begin(), x.end());
  float sum = 0;
  for (auto& v : x) {
    v = std::exp(v - max_val);
    sum += v;
  }
  for (auto& v : x) v /= sum;
}

size_t PolicyNetwork::ParameterCount() const {
  return w1_.size() + b1_.size() + w2_.size() + b2_.size();
}

size_t PolicyNetwork::MemoryBytes() const { return ParameterCount() * sizeof(float); }

// ==================== HybridPolicyStore ====================

HybridPolicyStore::HybridPolicyStore(size_t memory_node_limit) : memory_limit_(memory_node_limit) {
  policy_net_ = std::make_unique<PolicyNetwork>();
}

std::array<float, 5> HybridPolicyStore::GetPolicy(uint64_t infoset_key, const float* features) {
  // 首先尝试内存查找
  {
    std::shared_lock lock(mutex_);
    auto it = memory_nodes_.find(infoset_key);
    if (it != memory_nodes_.end()) {
      std::array<float, 5> policy;
      it->second.GetAverageStrategy(policy.data());
      return policy;
    }
  }

  // 回退到策略网络
  network_queries_++;
  return policy_net_->Forward(features);
}

void HybridPolicyStore::StoreNode(uint64_t key, const CFRStoreNode& node) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  if (memory_nodes_.size() >= memory_limit_) {
    // 内存已满，不存储（或替换 LRU 节点）
    // 策略网络将处理这些查询
    network_queries_++;
    return;
  }

  memory_nodes_[key] = node;
}

bool HybridPolicyStore::TryGetNode(uint64_t key, CFRStoreNode& out) const {
  std::shared_lock lock(mutex_);
  auto it = memory_nodes_.find(key);
  if (it == memory_nodes_.end()) return false;
  out = it->second;
  return true;
}

size_t HybridPolicyStore::NodeCount() const {
  std::shared_lock lock(mutex_);
  return memory_nodes_.size();
}

size_t HybridPolicyStore::NetworkCount() const { return network_queries_.load(); }

}  // namespace poker_engine::cfr
