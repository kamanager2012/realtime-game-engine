#pragma once

#include <random>

#include "ml_engine.h"

namespace poker_engine::anticheat {

// ==================== 合成数据生成器 ====================
// 用于训练 ML 模型，生成正常玩家和 Bot 的模拟行为数据

class MLDataGenerator {
 public:
  MLDataGenerator(uint32_t seed = 42);

  // ========== 生成正常玩家数据 ==========

  // 基于真实统计数据分布生成
  std::pair<PlayerFeatureVector, int> GenerateHumanSample(int hands_played_min = 50,
                                                          int hands_played_max = 2000);

  // ========== 生成 Bot 数据 ==========

  // 反应时间 Bot (恒定或接近恒定的响应时间)
  std::pair<PlayerFeatureVector, int> GenerateReactionBotSample();

  // Solver Bot (过于最优的游戏策略)
  std::pair<PlayerFeatureVector, int> GenerateSolverBotSample();

  // 串通 Bot (与同伙高度关联)
  std::pair<std::pair<PlayerFeatureVector, int>, std::pair<PlayerFeatureVector, int>>
  GenerateCollusionBotPair();

  // ========== 批量生成 ==========

  std::vector<std::pair<PlayerFeatureVector, int>> GenerateDataset(int human_samples = 1000,
                                                                   int reaction_bot_samples = 200,
                                                                   int solver_bot_samples = 200,
                                                                   int collusion_pairs = 100);

  // ========== 工具方法 ==========

  std::mt19937& GetRNG() { return rng_; }

 private:
  std::mt19937 rng_;

  // 辅助函数
  double NormalDistribution(double mean, double stddev);
  double Clamp(double value, double min_val, double max_val);

  // 模板：向特征向量添加噪声
  void AddNoise(PlayerFeatureVector& fv, double noise_level = 0.05);
};

}  // namespace poker_engine::anticheat
