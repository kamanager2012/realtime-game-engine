#pragma once
#include <cmath>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "poker_engine/anticheat/anticheat.h"

namespace poker_engine::anticheat {

// ==================== 特征向量 ====================

struct PlayerFeatureVector {
  static constexpr int kFeatureCount = 28;

  double features[kFeatureCount];
  int64_t player_id;

  // 特征索引
  enum FeatureIndex {
    FI_HANDS_PLAYED = 0,
    FI_WIN_RATE,
    FI_VPIP,
    FI_PFR,
    FI_AGG_FACTOR,
    FI_THREE_BET_PCT,
    FI_AVG_POT_PCT,
    FI_RESPONSE_TIME_MEAN,
    FI_RESPONSE_TIME_STDDEV,
    FI_RESPONSE_TIME_CV,
    FI_BET_SIZE_MEAN,
    FI_BET_SIZE_STDDEV,
    FI_POSITIONAL_VPIP_EP,
    FI_POSITIONAL_VPIP_MP,
    FI_POSITIONAL_VPIP_LP,
    FI_POSITIONAL_VPIP_BLIND,
    FI_POSITIONAL_PFR_EP,
    FI_POSITIONAL_PFR_MP,
    FI_POSITIONAL_PFR_LP,
    FI_POSITIONAL_PFR_BLIND,
    FI_SAME_TABLE_TOP1_RATE,
    FI_SAME_TABLE_TOP2_RATE,
    FI_ADJACENT_SEAT_TOP1_RATE,
    FI_SHOWDOWN_WIN_RATE,
    FI_FOLDED_TO_AGGRESSION,
    FI_WTSD_PCT,       // Went To Showdown %
    FI_WMSD_PCT,       // Won Money When Saw Flop %
    FI_BONUS_FEATURES  // 预留槽位
  };

  double& operator[](int i) { return features[i]; }
  double operator[](int i) const { return features[i]; }

  std::string ToString() const;
  static PlayerFeatureVector FromStats(const PlayerStatistics& stats);
};

// ==================== 决策树节点 ====================

struct DTNode {
  int feature_index = -1;   // 分裂特征
  double threshold = 0.0;   // 分裂阈值
  int label = -1;           // 叶节点标签 (-1=正常, 1=可疑)
  double confidence = 0.0;  // 预测置信度
  double impurity = 0.0;    // 不纯度 (Gini)
  int sample_count = 0;

  DTNode* left = nullptr;
  DTNode* right = nullptr;

  bool IsLeaf() const { return left == nullptr && right == nullptr; }
  ~DTNode() {
    delete left;
    delete right;
  }
};

// ==================== 随机森林分类器 ====================

class RandomForestClassifier {
 public:
  struct ForestConfig {
    int num_trees = 100;
    int max_depth = 8;
    int min_samples_split = 10;
    int min_samples_leaf = 5;
    int max_features = 5;  // sqrt(28) ≈ 5
    double subsample_ratio = 0.8;
    uint32_t seed = 42;
  };

  RandomForestClassifier();  // default ForestConfig
  explicit RandomForestClassifier(const ForestConfig& config);
  ~RandomForestClassifier();

  // 训练
  void Train(const std::vector<std::pair<PlayerFeatureVector, int>>& dataset);

  // 预测 (返回概率)
  double PredictProbability(const PlayerFeatureVector& features) const;

  // 预测 (返回标签)
  int Predict(const PlayerFeatureVector& features) const;

  // 序列化
  bool Save(const std::string& filepath) const;
  bool Load(const std::string& filepath);

  // 模型信息
  size_t TreeCount() const { return trees_.size(); }
  const ForestConfig& Config() const { return config_; }

  // 特征重要性
  std::vector<std::pair<int, double>> FeatureImportance() const;

 private:
  ForestConfig config_;
  std::vector<DTNode*> trees_;
  std::mt19937 rng_;

  // 训练辅助
  DTNode* BuildTree(const std::vector<size_t>& sample_indices,
                    const std::vector<std::pair<PlayerFeatureVector, int>>& data, int depth);

  int PredictTree(const DTNode* node, const PlayerFeatureVector& features) const;

  // Gini 不纯度
  static double GiniImpurity(const std::vector<int>& labels);
  static double GiniSplit(const std::vector<int>& left_labels,
                          const std::vector<int>& right_labels);

  // 采样
  std::vector<size_t> BootstrapSample(size_t total_size);

  // 最佳分裂
  struct SplitResult {
    int feature_index;
    double threshold;
    double gain;
  };
  SplitResult FindBestSplit(const std::vector<size_t>& indices,
                            const std::vector<std::pair<PlayerFeatureVector, int>>& data,
                            const std::vector<int>& feature_subset);

  // 序列化辅助
  void SerializeTree(std::ofstream& ofs, const DTNode* node) const;
  DTNode* DeserializeTree(std::ifstream& ifs);
  void ComputeFeatureImportance(const DTNode* node, std::vector<double>& importance) const;
};

// ML 分析结果
struct MLAnalysisResult {
  int64_t player_id;
  double bot_probability;                            // 0.0 ~ 1.0
  double collusion_probability;                      // 0.0 ~ 1.0
  double overall_risk_score;                         // 0.0 ~ 100.0
  std::vector<std::pair<int, double>> top_features;  // 影响最大的特征
  SuspicionLevel suspicion_level;

  std::string ToString() const;
};

// ==================== ML 反作弊引擎 ====================

class MLEntityDetector {
 public:
  MLEntityDetector();  // default ForestConfig
  explicit MLEntityDetector(const RandomForestClassifier::ForestConfig& config);

  // 加载训练数据
  void AddTrainingSample(const PlayerFeatureVector& features, int label);

  // 批量添加训练数据
  void AddTrainingData(const std::vector<std::pair<PlayerFeatureVector, int>>& data) {
    training_data_.insert(training_data_.end(), data.begin(), data.end());
  }

  // 训练模型
  void Train();

  // 分析玩家
  MLAnalysisResult Analyze(const PlayerStatistics& stats) const;

  // 模型持久化
  bool SaveModel(const std::string& path) const;
  bool LoadModel(const std::string& path);

  // 批量分析
  std::vector<MLAnalysisResult> AnalyzeBatch(
      const std::vector<const PlayerStatistics*>& stats_list) const;

  // 模型信息
  bool IsTrained() const { return classifier_ && classifier_->TreeCount() > 0; }
  size_t TreeCount() const { return classifier_ ? classifier_->TreeCount() : 0; }

  // 特征重要性
  std::vector<std::pair<int, double>> FeatureImportance() const {
    return classifier_ ? classifier_->FeatureImportance() : std::vector<std::pair<int, double>>{};
  }

 private:
  std::unique_ptr<RandomForestClassifier> classifier_;
  RandomForestClassifier::ForestConfig config_;
  std::vector<std::pair<PlayerFeatureVector, int>> training_data_;
  bool trained_ = false;

  double HeuristicBotScore(const PlayerStatistics& stats) const;
};

}  // namespace poker_engine::anticheat
