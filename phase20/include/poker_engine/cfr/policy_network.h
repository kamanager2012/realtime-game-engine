#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "poker_engine/cfr/cfr_store_node.h"
#include "poker_engine/cfr/types.h"

namespace poker_engine::cfr {

// ==================== 轻量级策略网络 ====================
// 2 层 MLP，从游戏状态直接输出行动概率
// 替代存储数十亿节点的查找表
// 应用于节点数爆炸的后期街

class PolicyNetwork {
 public:
  static constexpr int kInputSize = 32;
  // 特征: 手牌 bucket(1) + 街(1) + 底池量化(1) +
  //        投注历史(4) + 玩家筹码(1) + 位置(1) + 对手模型(23)
  static constexpr int kHiddenSize = 64;
  static constexpr int kOutputSize = 5;  // action count

  PolicyNetwork();

  // 前向传播
  std::array<float, kOutputSize> Forward(const float* input) const;
  std::array<float, kOutputSize> Forward(const std::array<float, kInputSize>& input) const;

  // 批量前向传播
  void ForwardBatch(const float* inputs, float* outputs, int batch_size) const;

  // 训练一步 (SGD)
  void TrainStep(const std::array<float, kInputSize>& input,
                 const std::array<float, kOutputSize>& target, float learning_rate = 0.001f);

  // 序列化
  bool Save(const std::string& path) const;
  bool Load(const std::string& path);

  // 参数统计
  size_t ParameterCount() const;
  size_t MemoryBytes() const;

 private:
  // 网络权重
  std::array<float, kInputSize * kHiddenSize> w1_;
  std::array<float, kHiddenSize> b1_;
  std::array<float, kHiddenSize * kOutputSize> w2_;
  std::array<float, kOutputSize> b2_;

  // 激活函数
  static float ReLU(float x) { return x > 0 ? x : 0; }
  static float LeakyReLU(float x) { return x > 0 ? x : 0.01f * x; }

  static void Softmax(std::array<float, kOutputSize>& x);

  // 初始化
  void XavierInit();
};

// ==================== 混合存储: 内存 + 网络 ====================
// 小树用 CFR 节点，大树用策略网络逼近

class HybridPolicyStore {
 public:
  explicit HybridPolicyStore(size_t memory_node_limit = 5000000);

  // 查询策略
  std::array<float, 5> GetPolicy(uint64_t infoset_key, const float* features);

  // 存储节点
  void StoreNode(uint64_t key, const CFRStoreNode& node);

  // 获取节点 (混合查询)
  bool TryGetNode(uint64_t key, CFRStoreNode& out) const;

  // 统计
  size_t NodeCount() const;
  size_t NetworkCount() const;  // 使用网络逼近的查询次数

 private:
  size_t memory_limit_;
  std::unordered_map<uint64_t, CFRStoreNode> memory_nodes_;
  std::unique_ptr<PolicyNetwork> policy_net_;
  mutable std::atomic<size_t> network_queries_{0};
  mutable std::shared_mutex mutex_;
};

}  // namespace poker_engine::cfr
