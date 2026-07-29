#include "poker_engine/anticheat/ml_engine.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::anticheat {

namespace {

// ==================== 数学工具 ====================

double Mean(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  double sum = 0.0;
  for (auto x : v) sum += x;
  return sum / v.size();
}

double StdDev(const std::vector<double>& v, double mean) {
  if (v.size() < 2) return 0.0;
  double sum = 0.0;
  for (auto x : v) sum += (x - mean) * (x - mean);
  return std::sqrt(sum / (v.size() - 1));
}

double ComputeGiniImpurity(const std::vector<int>& labels) {
  if (labels.empty()) return 0.0;

  std::unordered_map<int, int> counts;
  for (int l : labels) counts[l]++;

  double impurity = 1.0;
  double n = static_cast<double>(labels.size());
  for (auto& [label, count] : counts) {
    double p = count / n;
    impurity -= p * p;
  }
  return impurity;
}

double ComputeGiniSplit(const std::vector<int>& left, const std::vector<int>& right) {
  size_t total = left.size() + right.size();
  if (total == 0) return 0.0;

  double p_left = static_cast<double>(left.size()) / total;
  double p_right = static_cast<double>(right.size()) / total;

  return p_left * ComputeGiniImpurity(left) + p_right * ComputeGiniImpurity(right);
}

std::vector<size_t> MakeIndexVector(size_t n) {
  std::vector<size_t> idx(n);
  std::iota(idx.begin(), idx.end(), 0);
  return idx;
}

}  // namespace

// ==================== PlayerFeatureVector ====================

PlayerFeatureVector PlayerFeatureVector::FromStats(const PlayerStatistics& stats) {
  PlayerFeatureVector fv;
  fv.player_id = stats.player_id;
  std::fill(std::begin(fv.features), std::end(fv.features), 0.0);

  // 归一化特征到 [0, 1]
  fv[FI_HANDS_PLAYED] = std::min(stats.hands_played / 1000.0, 1.0);
  fv[FI_WIN_RATE] =
      stats.hands_played > 0 ? stats.hands_won / static_cast<double>(stats.hands_played) : 0.5;
  fv[FI_VPIP] = stats.vpip_pct / 100.0;
  fv[FI_PFR] = stats.pfr_pct / 100.0;
  fv[FI_AGG_FACTOR] = std::min(stats.agg_factor / 5.0, 1.0);
  fv[FI_THREE_BET_PCT] = stats.three_bet_pct / 100.0;

  // 响应时间特征
  if (!stats.response_times_ms.empty()) {
    std::vector<double> rt(stats.response_times_ms.begin(), stats.response_times_ms.end());
    double mean_rt = Mean(rt);
    double stddev_rt = StdDev(rt, mean_rt);
    fv[FI_RESPONSE_TIME_MEAN] = mean_rt / 30000.0;  // 归一化到 30s
    fv[FI_RESPONSE_TIME_STDDEV] = stddev_rt / 30000.0;
    fv[FI_RESPONSE_TIME_CV] = (mean_rt > 0) ? (stddev_rt / mean_rt) : 0.0;
    fv[FI_RESPONSE_TIME_CV] = std::min(fv[FI_RESPONSE_TIME_CV], 1.0);
  }

  // 下注尺度特征
  if (!stats.bet_sizing_history.empty()) {
    double mean_bs = Mean(stats.bet_sizing_history);
    double stddev_bs = StdDev(stats.bet_sizing_history, mean_bs);
    fv[FI_BET_SIZE_MEAN] = mean_bs;
    fv[FI_BET_SIZE_STDDEV] = std::min(stddev_bs, 1.0);
  }

  // 位置特征
  auto pos_ratio = [](const PlayerStatistics::PositionalStats& ps) {
    return ps.hands > 0 ? ps.vpip / static_cast<double>(ps.hands) : 0.5;
  };
  fv[FI_POSITIONAL_VPIP_EP] = pos_ratio(stats.early) / 100.0;
  fv[FI_POSITIONAL_VPIP_MP] = pos_ratio(stats.middle) / 100.0;
  fv[FI_POSITIONAL_VPIP_LP] = pos_ratio(stats.late) / 100.0;
  fv[FI_POSITIONAL_VPIP_BLIND] = pos_ratio(stats.blind) / 100.0;

  // 同桌统计
  int max_same_table = 0;
  int max_adjacent = 0;
  for (auto& [_, count] : stats.same_table_counts) {
    max_same_table = std::max(max_same_table, count);
  }
  for (auto& [_, count] : stats.adjacent_seat_counts) {
    max_adjacent = std::max(max_adjacent, count);
  }
  fv[FI_SAME_TABLE_TOP1_RATE] = std::min(max_same_table / 50.0, 1.0);
  fv[FI_ADJACENT_SEAT_TOP1_RATE] = std::min(max_adjacent / 50.0, 1.0);

  return fv;
}

std::string PlayerFeatureVector::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3);
  oss << "[";
  for (int i = 0; i < kFeatureCount; ++i) {
    if (i > 0) oss << ", ";
    oss << features[i];
  }
  oss << "]";
  return oss.str();
}

// ==================== RandomForestClassifier ====================

RandomForestClassifier::RandomForestClassifier() : RandomForestClassifier(ForestConfig{}) {}

RandomForestClassifier::RandomForestClassifier(const ForestConfig& config)
    : config_(config), rng_(config.seed) {}

RandomForestClassifier::~RandomForestClassifier() {
  for (auto* tree : trees_) delete tree;
}

void RandomForestClassifier::Train(
    const std::vector<std::pair<PlayerFeatureVector, int>>& dataset) {
  PE_LOG_INFO("Training RandomForest: {} samples, {} trees, max_depth={}", dataset.size(),
              config_.num_trees, config_.max_depth);

  // 清理旧树
  for (auto* tree : trees_) delete tree;
  trees_.clear();

  for (int t = 0; t < config_.num_trees; ++t) {
    // Bootstrap 采样
    auto bootstrap = BootstrapSample(dataset.size());

    // 构建决策树
    DTNode* root = BuildTree(bootstrap, dataset, 0);
    trees_.push_back(root);

    if ((t + 1) % 20 == 0) {
      PE_LOG_INFO("  Built tree {}/{}", t + 1, config_.num_trees);
    }
  }

  PE_LOG_INFO("Training complete: {} trees built", trees_.size());
}

double RandomForestClassifier::PredictProbability(const PlayerFeatureVector& features) const {
  if (trees_.empty()) return 0.5;

  int cheat_votes = 0;
  for (const auto* tree : trees_) {
    if (PredictTree(tree, features) == 1) cheat_votes++;
  }

  return static_cast<double>(cheat_votes) / trees_.size();
}

int RandomForestClassifier::Predict(const PlayerFeatureVector& features) const {
  double prob = PredictProbability(features);
  return prob >= 0.5 ? 1 : -1;
}

auto RandomForestClassifier::BuildTree(const std::vector<size_t>& sample_indices,
                                       const std::vector<std::pair<PlayerFeatureVector, int>>& data,
                                       int depth) -> DTNode* {
  DTNode* node = new DTNode();

  // 收集所有标签
  std::vector<int> labels;
  for (auto idx : sample_indices) labels.push_back(data[idx].second);

  node->sample_count = static_cast<int>(sample_indices.size());
  node->impurity = ComputeGiniImpurity(labels);

  // 终止条件
  if (depth >= config_.max_depth ||
      sample_indices.size() < static_cast<size_t>(config_.min_samples_split) ||
      node->impurity < 1e-6) {
    // 叶节点
    int cheat_count = std::count(labels.begin(), labels.end(), 1);
    node->label = (cheat_count * 2 >= static_cast<int>(labels.size())) ? 1 : -1;
    node->confidence =
        static_cast<double>(std::max(cheat_count, static_cast<int>(labels.size()) - cheat_count)) /
        labels.size();
    return node;
  }

  // 随机特征子集
  std::vector<int> all_features(PlayerFeatureVector::kFeatureCount);
  std::iota(all_features.begin(), all_features.end(), 0);
  std::shuffle(all_features.begin(), all_features.end(), rng_);

  int num_features = std::min(config_.max_features, static_cast<int>(all_features.size()));
  std::vector<int> feature_subset(all_features.begin(), all_features.begin() + num_features);

  // 寻找最佳分裂
  auto split = FindBestSplit(sample_indices, data, feature_subset);

  if (split.gain < 1e-8) {
    // 无有效分裂，创建叶节点
    int cheat_count = std::count(labels.begin(), labels.end(), 1);
    node->label = (cheat_count * 2 >= static_cast<int>(labels.size())) ? 1 : -1;
    node->confidence =
        static_cast<double>(std::max(cheat_count, static_cast<int>(labels.size()) - cheat_count)) /
        labels.size();
    return node;
  }

  node->feature_index = split.feature_index;
  node->threshold = split.threshold;

  // 分割样本
  std::vector<size_t> left_indices, right_indices;
  for (auto idx : sample_indices) {
    if (data[idx].first[split.feature_index] <= split.threshold) {
      left_indices.push_back(idx);
    } else {
      right_indices.push_back(idx);
    }
  }

  // 递归构建子树
  if (left_indices.size() >= static_cast<size_t>(config_.min_samples_leaf)) {
    node->left = BuildTree(left_indices, data, depth + 1);
  } else {
    node->left = new DTNode();
    int cheat_count = 0;
    for (auto idx : left_indices) cheat_count += (data[idx].second == 1);
    node->left->label = (cheat_count > 0) ? 1 : -1;
    node->left->confidence =
        left_indices.empty()
            ? 0.5
            : static_cast<double>(
                  std::max(cheat_count, static_cast<int>(left_indices.size()) - cheat_count)) /
                  left_indices.size();
    node->left->sample_count = static_cast<int>(left_indices.size());
  }

  if (right_indices.size() >= static_cast<size_t>(config_.min_samples_leaf)) {
    node->right = BuildTree(right_indices, data, depth + 1);
  } else {
    node->right = new DTNode();
    int cheat_count = 0;
    for (auto idx : right_indices) cheat_count += (data[idx].second == 1);
    node->right->label = (cheat_count > 0) ? 1 : -1;
    node->right->confidence =
        right_indices.empty()
            ? 0.5
            : static_cast<double>(
                  std::max(cheat_count, static_cast<int>(right_indices.size()) - cheat_count)) /
                  right_indices.size();
    node->right->sample_count = static_cast<int>(right_indices.size());
  }

  return node;
}

int RandomForestClassifier::PredictTree(const DTNode* node,
                                        const PlayerFeatureVector& features) const {
  if (node->IsLeaf()) return node->label;

  if (features[node->feature_index] <= node->threshold) {
    return PredictTree(node->left, features);
  } else {
    return PredictTree(node->right, features);
  }
}

double RandomForestClassifier::GiniImpurity(const std::vector<int>& labels) {
  return ComputeGiniImpurity(labels);
}

double RandomForestClassifier::GiniSplit(const std::vector<int>& left,
                                         const std::vector<int>& right) {
  return ComputeGiniSplit(left, right);
}

std::vector<size_t> RandomForestClassifier::BootstrapSample(size_t total_size) {
  std::vector<size_t> sample;
  std::uniform_int_distribution<size_t> dist(0, total_size - 1);
  size_t sample_size = static_cast<size_t>(total_size * config_.subsample_ratio);

  for (size_t i = 0; i < sample_size; ++i) {
    sample.push_back(dist(rng_));
  }
  return sample;
}

RandomForestClassifier::SplitResult RandomForestClassifier::FindBestSplit(
    const std::vector<size_t>& indices,
    const std::vector<std::pair<PlayerFeatureVector, int>>& data,
    const std::vector<int>& feature_subset) {
  SplitResult best{0, 0.0, -1.0};

  // 当前节点纯度
  std::vector<int> all_labels;
  for (auto idx : indices) all_labels.push_back(data[idx].second);
  double parent_impurity = ComputeGiniImpurity(all_labels);

  for (int feat : feature_subset) {
    // 收集该特征的所有值
    std::vector<std::pair<double, int>> values;
    for (auto idx : indices) {
      values.push_back({data[idx].first[feat], data[idx].second});
    }
    std::sort(values.begin(), values.end());

    // 尝试所有可能的分裂点
    for (size_t i = 1; i < values.size(); ++i) {
      if (values[i].first == values[i - 1].first) continue;

      double threshold = (values[i - 1].first + values[i].first) / 2.0;

      std::vector<int> left_labels, right_labels;
      for (auto& [val, label] : values) {
        if (val <= threshold)
          left_labels.push_back(label);
        else
          right_labels.push_back(label);
      }

      double split_impurity = ComputeGiniSplit(left_labels, right_labels);
      double gain = parent_impurity - split_impurity;

      if (gain > best.gain) {
        best.feature_index = feat;
        best.threshold = threshold;
        best.gain = gain;
      }
    }
  }

  return best;
}

bool RandomForestClassifier::Save(const std::string& filepath) const {
  std::ofstream ofs(filepath, std::ios::binary);
  if (!ofs) return false;

  // Header
  uint32_t magic = 0x52464F52;  // "RFOR"
  uint32_t version = 1;
  uint32_t num_trees = static_cast<uint32_t>(trees_.size());

  ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
  ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));
  ofs.write(reinterpret_cast<const char*>(&num_trees), sizeof(num_trees));
  ofs.write(reinterpret_cast<const char*>(&config_), sizeof(config_));

  // 序列化每棵决策树（中序遍历）
  for (const auto* tree : trees_) {
    SerializeTree(ofs, tree);
  }

  return ofs.good();
}

bool RandomForestClassifier::Load(const std::string& filepath) {
  std::ifstream ifs(filepath, std::ios::binary);
  if (!ifs) return false;

  uint32_t magic, version, num_trees;
  ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  if (magic != 0x52464F52) return false;

  ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
  ifs.read(reinterpret_cast<char*>(&num_trees), sizeof(num_trees));
  ifs.read(reinterpret_cast<char*>(&config_), sizeof(config_));

  for (auto* tree : trees_) delete tree;
  trees_.clear();

  for (uint32_t i = 0; i < num_trees; ++i) {
    DTNode* tree = DeserializeTree(ifs);
    if (!tree) return false;
    trees_.push_back(tree);
  }

  return ifs.good();
}

void RandomForestClassifier::SerializeTree(std::ofstream& ofs, const DTNode* node) const {
  if (!node) {
    uint8_t marker = 0;
    ofs.write(reinterpret_cast<const char*>(&marker), 1);
    return;
  }

  uint8_t marker = 1;
  ofs.write(reinterpret_cast<const char*>(&marker), 1);

  uint8_t is_leaf = node->IsLeaf() ? 1 : 0;
  ofs.write(reinterpret_cast<const char*>(&is_leaf), 1);

  if (is_leaf) {
    ofs.write(reinterpret_cast<const char*>(&node->label), sizeof(node->label));
    ofs.write(reinterpret_cast<const char*>(&node->confidence), sizeof(node->confidence));
    ofs.write(reinterpret_cast<const char*>(&node->sample_count), sizeof(node->sample_count));
  } else {
    ofs.write(reinterpret_cast<const char*>(&node->feature_index), sizeof(node->feature_index));
    ofs.write(reinterpret_cast<const char*>(&node->threshold), sizeof(node->threshold));
    SerializeTree(ofs, node->left);
    SerializeTree(ofs, node->right);
  }
}

auto RandomForestClassifier::DeserializeTree(std::ifstream& ifs) -> DTNode* {
  uint8_t marker;
  ifs.read(reinterpret_cast<char*>(&marker), 1);
  if (marker == 0) return nullptr;

  DTNode* node = new DTNode();

  uint8_t is_leaf;
  ifs.read(reinterpret_cast<char*>(&is_leaf), 1);

  if (is_leaf) {
    ifs.read(reinterpret_cast<char*>(&node->label), sizeof(node->label));
    ifs.read(reinterpret_cast<char*>(&node->confidence), sizeof(node->confidence));
    ifs.read(reinterpret_cast<char*>(&node->sample_count), sizeof(node->sample_count));
  } else {
    ifs.read(reinterpret_cast<char*>(&node->feature_index), sizeof(node->feature_index));
    ifs.read(reinterpret_cast<char*>(&node->threshold), sizeof(node->threshold));
    node->left = DeserializeTree(ifs);
    node->right = DeserializeTree(ifs);
  }

  return node;
}

std::vector<std::pair<int, double>> RandomForestClassifier::FeatureImportance() const {
  std::vector<double> importance(PlayerFeatureVector::kFeatureCount, 0.0);

  for (const auto* tree : trees_) {
    ComputeFeatureImportance(tree, importance);
  }

  std::vector<std::pair<int, double>> result;
  for (int i = 0; i < PlayerFeatureVector::kFeatureCount; ++i) {
    if (importance[i] > 0) {
      result.emplace_back(i, importance[i] / trees_.size());
    }
  }

  std::sort(result.begin(), result.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  return result;
}

void RandomForestClassifier::ComputeFeatureImportance(const DTNode* node,
                                                      std::vector<double>& importance) const {
  if (!node || node->IsLeaf()) return;

  importance[node->feature_index] += node->impurity * node->sample_count;
  if (node->left)
    importance[node->feature_index] -= node->left->impurity * node->left->sample_count;
  if (node->right)
    importance[node->feature_index] -= node->right->impurity * node->right->sample_count;

  ComputeFeatureImportance(node->left, importance);
  ComputeFeatureImportance(node->right, importance);
}

// ==================== MLEntityDetector ====================

MLEntityDetector::MLEntityDetector() : MLEntityDetector(RandomForestClassifier::ForestConfig{}) {}

MLEntityDetector::MLEntityDetector(const RandomForestClassifier::ForestConfig& config)
    : classifier_(std::make_unique<RandomForestClassifier>(config)), config_(config) {}

void MLEntityDetector::AddTrainingSample(const PlayerFeatureVector& features, int label) {
  training_data_.push_back({features, label});
}

void MLEntityDetector::Train() {
  if (training_data_.empty()) {
    PE_LOG_WARN("ML: No training data available");
    return;
  }

  PE_LOG_INFO("ML: Training on {} samples", training_data_.size());

  // 正负样本平衡
  int pos_count = 0, neg_count = 0;
  for (auto& [fv, label] : training_data_) {
    if (label == 1)
      pos_count++;
    else
      neg_count++;
  }

  if (pos_count == 0) {
    PE_LOG_WARN("ML: No positive (cheat) samples - using unsupervised approach");
    trained_ = true;
    return;
  }

  classifier_->Train(training_data_);
  trained_ = true;

  PE_LOG_INFO("ML: Training complete (trees={})", classifier_->TreeCount());
}

MLAnalysisResult MLEntityDetector::Analyze(const PlayerStatistics& stats) const {
  MLAnalysisResult result;
  result.player_id = stats.player_id;

  PlayerFeatureVector fv = PlayerFeatureVector::FromStats(stats);

  if (trained_ && classifier_->TreeCount() > 0) {
    result.bot_probability = classifier_->PredictProbability(fv);
  } else {
    // 无模型时的启发式
    result.bot_probability = HeuristicBotScore(stats);
  }

  // Collusion 检测简化为同桌率
  double max_same_table = 0;
  for (auto& [_, count] : stats.same_table_counts) {
    if (count > max_same_table) max_same_table = count;
  }
  double same_table_rate = (stats.hands_played > 0) ? max_same_table / stats.hands_played : 0;
  result.collusion_probability = std::min(1.0, same_table_rate * 2.0);

  // 综合风险评分
  result.overall_risk_score = (result.bot_probability * 50.0 + result.collusion_probability * 50.0);

  // 怀疑等级
  if (result.overall_risk_score >= 60)
    result.suspicion_level = SuspicionLevel::Confirmed;
  else if (result.overall_risk_score >= 40)
    result.suspicion_level = SuspicionLevel::High;
  else if (result.overall_risk_score >= 20)
    result.suspicion_level = SuspicionLevel::Medium;
  else if (result.overall_risk_score >= 5)
    result.suspicion_level = SuspicionLevel::Low;
  else
    result.suspicion_level = SuspicionLevel::Clean;

  // Top 特征
  auto importance = classifier_->FeatureImportance();
  for (size_t i = 0; i < std::min(importance.size(), size_t(5)); ++i) {
    result.top_features.push_back(importance[i]);
  }

  return result;
}

double MLEntityDetector::HeuristicBotScore(const PlayerStatistics& stats) const {
  double score = 0.0;

  // 响应时间一致性 (40% 权重)
  if (!stats.response_times_ms.empty()) {
    std::vector<double> rt(stats.response_times_ms.begin(), stats.response_times_ms.end());
    double mean = Mean(rt);
    double stddev = StdDev(rt, mean);
    double cv = (mean > 0) ? stddev / mean : 0;
    if (cv < 0.1)
      score += 0.8 * 0.4;
    else if (cv < 0.3)
      score += 0.3 * 0.4;
  }

  // 最优游戏频率 (30%)
  if (stats.vpip_pct > 0) {
    double ratio = stats.pfr_pct / stats.vpip_pct;
    if (ratio > 0.85 && ratio < 0.95) score += 0.6 * 0.3;
  }

  // 下注尺度精确度 (20%)
  if (stats.bet_sizing_history.size() > 20) {
    // 检查是否总是使用整数尺度
    int round_count = 0;
    for (double s : stats.bet_sizing_history) {
      if (std::abs(s * 100 - std::round(s * 100)) < 0.01) round_count++;
    }
    double round_ratio = round_count / static_cast<double>(stats.bet_sizing_history.size());
    if (round_ratio > 0.95) score += 0.5 * 0.2;
  }

  // 位置意识 (10%)
  // 简化

  return score;
}

bool MLEntityDetector::SaveModel(const std::string& path) const { return classifier_->Save(path); }

bool MLEntityDetector::LoadModel(const std::string& path) {
  bool ok = classifier_->Load(path);
  if (ok) trained_ = true;
  return ok;
}

std::vector<MLAnalysisResult> MLEntityDetector::AnalyzeBatch(
    const std::vector<const PlayerStatistics*>& stats_list) const {
  std::vector<MLAnalysisResult> results;
  results.reserve(stats_list.size());

  for (auto* stats : stats_list) {
    results.push_back(Analyze(*stats));
  }

  return results;
}

// MLAnalysisResult
std::string MLAnalysisResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "Player " << player_id << ": ";
  oss << "bot=" << bot_probability * 100 << "% ";
  oss << "collab=" << collusion_probability * 100 << "% ";
  oss << "risk=" << overall_risk_score;
  oss << " level=";
  switch (suspicion_level) {
    case SuspicionLevel::Clean:
      oss << "CLEAN";
      break;
    case SuspicionLevel::Low:
      oss << "LOW";
      break;
    case SuspicionLevel::Medium:
      oss << "MEDIUM";
      break;
    case SuspicionLevel::High:
      oss << "HIGH";
      break;
    case SuspicionLevel::Confirmed:
      oss << "CONFIRMED";
      break;
  }
  return oss.str();
}

}  // namespace poker_engine::anticheat
