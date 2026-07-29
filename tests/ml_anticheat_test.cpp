#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <iostream>

#include "poker_engine/anticheat/ml_data_generator.h"
#include "poker_engine/anticheat/ml_engine.h"

using namespace poker_engine::anticheat;

class MLEntityDetectorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    RandomForestClassifier::ForestConfig config;
    config.num_trees = 50;
    config.max_depth = 6;
    config.min_samples_split = 5;
    config.min_samples_leaf = 2;
    config.subsample_ratio = 0.8;

    detector_ = std::make_unique<MLEntityDetector>(config);
  }

  std::unique_ptr<MLEntityDetector> detector_;
};

TEST_F(MLEntityDetectorTest, FeatureVectorFromStats) {
  PlayerStatistics stats;
  stats.player_id = 1;
  stats.hands_played = 200;
  stats.hands_won = 50;
  stats.vpip_pct = 25.0;
  stats.pfr_pct = 20.0;
  stats.agg_factor = 1.5;
  stats.three_bet_pct = 8.0;
  stats.bet_sizing_history = std::vector<double>(200, 0.7);
  stats.response_times_ms = std::vector<int64_t>(200, 2000);

  stats.early.hands = 50;
  stats.early.vpip = 15;
  stats.middle.hands = 50;
  stats.middle.vpip = 20;
  stats.late.hands = 50;
  stats.late.vpip = 30;
  stats.blind.hands = 50;
  stats.blind.vpip = 35;

  auto fv = PlayerFeatureVector::FromStats(stats);

  EXPECT_NEAR(fv[PlayerFeatureVector::FI_VPIP], 0.25, 0.01);
  EXPECT_NEAR(fv[PlayerFeatureVector::FI_PFR], 0.20, 0.01);
  EXPECT_NEAR(fv[PlayerFeatureVector::FI_HANDS_PLAYED], 0.2, 0.01);

  std::cout << "Feature vector: " << fv.ToString() << std::endl;
}

TEST_F(MLEntityDetectorTest, DataGenerator) {
  MLDataGenerator gen(42);

  // 生成数据集
  auto dataset = gen.GenerateDataset(500, 100, 100, 50);

  EXPECT_EQ(dataset.size(), 500u + 100u + 100u + 100u);  // 含成对的 collusion

  // 统计标签分布
  int pos = 0, neg = 0;
  for (auto& [fv, label] : dataset) {
    if (label == 1)
      pos++;
    else
      neg++;
  }

  std::cout << "Dataset: " << dataset.size() << " samples (" << neg << " human, " << pos
            << " cheat)" << std::endl;

  EXPECT_GT(pos, 0);
  EXPECT_GT(neg, 0);
}

TEST_F(MLEntityDetectorTest, TrainAndPredict) {
  MLDataGenerator gen(42);

  // 小规模训练数据
  auto dataset = gen.GenerateDataset(300, 50, 50, 25);

  detector_->AddTrainingData(dataset);
  detector_->Train();

  ASSERT_TRUE(detector_->IsTrained());
  EXPECT_GT(detector_->TreeCount(), 0u);

  // 测试正常玩家
  PlayerStatistics normal_stats;
  normal_stats.player_id = 1;
  normal_stats.hands_played = 200;
  normal_stats.vpip_pct = 25.0;
  normal_stats.pfr_pct = 20.0;
  normal_stats.agg_factor = 1.5;
  normal_stats.bet_sizing_history = std::vector<double>(200);
  normal_stats.response_times_ms = std::vector<int64_t>(200);

  std::mt19937 rng(42);
  for (int i = 0; i < 200; ++i) {
    std::normal_distribution<double> dist(7.0, 0.8);
    normal_stats.response_times_ms[i] = static_cast<int64_t>(std::exp(dist(rng)));
    std::uniform_real_distribution<double> bet_dist(0.3, 1.0);
    normal_stats.bet_sizing_history[i] = bet_dist(rng);
  }

  auto result = detector_->Analyze(normal_stats);
  EXPECT_LT(result.overall_risk_score, 50.0) << "Normal player should have low risk score";

  std::cout << "Normal player risk: " << result.overall_risk_score << std::endl;

  // 测试 Bot
  PlayerStatistics bot_stats;
  bot_stats.player_id = 2;
  bot_stats.hands_played = 500;
  bot_stats.vpip_pct = 28.0;
  bot_stats.pfr_pct = 26.5;
  bot_stats.agg_factor = 1.2;

  // 恒定响应时间
  bot_stats.response_times_ms = std::vector<int64_t>(500, 2000);
  bot_stats.bet_sizing_history = std::vector<double>(500, 0.65);

  bot_stats.early.hands = 125;
  bot_stats.early.vpip = 35;
  bot_stats.middle.hands = 125;
  bot_stats.middle.vpip = 35;
  bot_stats.late.hands = 125;
  bot_stats.late.vpip = 35;
  bot_stats.blind.hands = 125;
  bot_stats.blind.vpip = 35;

  auto bot_result = detector_->Analyze(bot_stats);
  EXPECT_GT(bot_result.overall_risk_score, 20.0) << "Bot should have high risk score";

  std::cout << "Bot risk: " << bot_result.overall_risk_score << std::endl;
}

TEST_F(MLEntityDetectorTest, ModelPersistence) {
  MLDataGenerator gen(42);
  auto dataset = gen.GenerateDataset(200, 30, 30, 15);

  detector_->AddTrainingData(dataset);
  detector_->Train();

  // 保存
  EXPECT_TRUE(detector_->SaveModel("/tmp/ml_model.bin"));

  // 加载到新实例
  RandomForestClassifier::ForestConfig config;
  auto detector2 = std::make_unique<MLEntityDetector>(config);
  EXPECT_TRUE(detector2->LoadModel("/tmp/ml_model.bin"));

  // 验证预测一致性
  PlayerStatistics stats;
  stats.player_id = 99;
  stats.hands_played = 100;
  stats.vpip_pct = 30.0;
  stats.pfr_pct = 25.0;
  stats.response_times_ms = std::vector<int64_t>(100, 3000);
  stats.bet_sizing_history = std::vector<double>(100, 0.8);

  auto r1 = detector_->Analyze(stats);
  auto r2 = detector2->Analyze(stats);

  EXPECT_NEAR(r1.overall_risk_score, r2.overall_risk_score, 0.1);

  std::filesystem::remove("/tmp/ml_model.bin");
}

TEST_F(MLEntityDetectorTest, EndToEndPipeline) {
  // 模拟完整的反作弊分析管线

  MLDataGenerator gen(123);

  // Step 1: 生成训练数据
  auto train_data = gen.GenerateDataset(1000, 200, 200, 100);
  detector_->AddTrainingData(train_data);

  // Step 2: 训练
  detector_->Train();
  ASSERT_TRUE(detector_->IsTrained());

  // Step 3: 批量分析
  std::vector<const PlayerStatistics*> test_batch;
  std::vector<PlayerStatistics> test_stats;
  test_stats.reserve(50);

  for (int i = 0; i < 50; ++i) {
    PlayerStatistics ps;
    ps.player_id = 1000 + i;
    ps.hands_played = 100 + (i % 10) * 50;
    ps.vpip_pct = 20.0 + (i % 15);
    ps.pfr_pct = 15.0 + (i % 12);
    ps.response_times_ms.resize(ps.hands_played, 2000 + (i * 100));
    ps.response_times_ms[0] = 3000;
    ps.bet_sizing_history.resize(ps.hands_played, 0.6 + i * 0.01);

    ps.early.hands = ps.hands_played / 4;
    ps.early.vpip = static_cast<int>(ps.vpip_pct / 100.0 * ps.early.hands);
    ps.middle = ps.early;
    ps.late = ps.early;
    ps.blind = ps.early;

    test_stats.push_back(ps);
    test_batch.push_back(&test_stats.back());
  }

  auto results = detector_->AnalyzeBatch(test_batch);

  EXPECT_EQ(results.size(), 50u);

  // 验证所有结果都有效
  for (auto& r : results) {
    EXPECT_GE(r.bot_probability, 0.0);
    EXPECT_LE(r.bot_probability, 1.0);
    EXPECT_GE(r.collusion_probability, 0.0);
    EXPECT_LE(r.collusion_probability, 1.0);
    EXPECT_GE(r.overall_risk_score, 0.0);
    EXPECT_LE(r.overall_risk_score, 100.0);
  }

  // 统计可疑玩家数量
  int flagged = 0;
  for (auto& r : results) {
    if (r.suspicion_level > SuspicionLevel::Clean) flagged++;
  }

  std::cout << "Batch analysis: " << flagged << " / " << results.size() << " flagged" << std::endl;
}

TEST_F(MLEntityDetectorTest, Performance_1000Players) {
  MLDataGenerator gen(42);
  auto dataset = gen.GenerateDataset(1000, 200, 200, 100);
  detector_->AddTrainingData(dataset);
  detector_->Train();

  // 批量分析 1000 玩家
  std::vector<PlayerStatistics> batch;
  std::vector<const PlayerStatistics*> ptrs;
  batch.reserve(1000);

  for (int i = 0; i < 1000; ++i) {
    PlayerStatistics ps;
    ps.player_id = i;
    ps.hands_played = 100 + (i % 200);
    ps.vpip_pct = 15.0 + (i % 30);
    ps.pfr_pct = 12.0 + (i % 25);
    ps.response_times_ms.resize(ps.hands_played, 1500 + (i % 5000));
    ps.bet_sizing_history.resize(ps.hands_played, 0.5 + (i % 50) * 0.01);

    ps.early.hands = ps.hands_played / 4;
    ps.early.vpip = ps.hands_played / 5;
    ps.middle = ps.early;
    ps.late = ps.early;
    ps.blind = ps.early;

    batch.push_back(ps);
    ptrs.push_back(&batch.back());
  }

  auto start = std::chrono::high_resolution_clock::now();
  auto results = detector_->AnalyzeBatch(ptrs);
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::high_resolution_clock::now() - start)
                     .count();

  std::cout << "\nML Analysis: 1000 players in " << elapsed << "ms (" << (1000.0 * 1000.0 / elapsed)
            << " players/sec)" << std::endl;

  // 期望：< 5 秒
  EXPECT_LT(elapsed, 5000);
}

TEST_F(MLEntityDetectorTest, FeatureImportance) {
  MLDataGenerator gen(42);
  auto dataset = gen.GenerateDataset(500, 100, 100, 50);
  detector_->AddTrainingData(dataset);
  detector_->Train();

  auto importance = detector_->FeatureImportance();

  ASSERT_FALSE(importance.empty());

  std::cout << "\nTop 5 features:" << std::endl;
  for (size_t i = 0; i < std::min(importance.size(), size_t(5)); ++i) {
    std::cout << "  " << i + 1 << ". Feature " << importance[i].first << ": "
              << importance[i].second << std::endl;
  }
}
