#include <gtest/gtest.h>

#include <filesystem>
#include <thread>

#include "poker_engine/anticheat/ml_data_generator.h"
#include "poker_engine/anticheat/ml_engine.h"
#include "poker_engine/spectator/spectator_manager.h"

using namespace poker_engine;

class SpectatorE2ETest : public ::testing::Test {
 protected:
  void SetUp() override { spectator_mgr_ = std::make_unique<spectator::SpectatorManager>(); }

  std::unique_ptr<spectator::SpectatorManager> spectator_mgr_;
  network::SessionManager session_mgr_;
};

TEST_F(SpectatorE2ETest, SpectatorManagerStartStop) {
  spectator::SpectatorConfig config;
  config.spectator_port = 19002;
  spectator::SpectatorManager mgr(config);

  EXPECT_TRUE(mgr.Start());
  mgr.Stop();
}

TEST_F(SpectatorE2ETest, SpectatorEventSerializeDeserialize) {
  spectator::SpectatorEvent evt;
  evt.sequence_id = 42;
  evt.tournament_id = 100;
  evt.hand_id = 5;
  evt.type = spectator::SpectatorMessageType::HandEvent;
  evt.timestamp_ms = 1234567890.0;
  evt.payload = R"({"action":"raise","amount":100})";

  auto serialized = evt.Serialize();
  auto deserialized = spectator::SpectatorEvent::Deserialize(serialized);

  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->sequence_id, 42u);
  EXPECT_EQ(deserialized->tournament_id, 100u);
  EXPECT_EQ(deserialized->hand_id, 5u);
  EXPECT_EQ(deserialized->type, spectator::SpectatorMessageType::HandEvent);
}

TEST_F(SpectatorE2ETest, SpectatorStats) {
  auto stats = spectator_mgr_->GetStats();
  EXPECT_EQ(stats.total_sessions, 0);
  EXPECT_EQ(stats.active_sessions, 0);
  EXPECT_EQ(stats.events_broadcast, 0);
}

TEST_F(SpectatorE2ETest, MLTrainingPipeline) {
  anticheat::MLDataGenerator gen(42);

  // Step 1: 生成合成数据
  auto dataset = gen.GenerateDataset(1000,  // 正常玩家
                                     200,   // 反应 Bot
                                     200,   // Solver Bot
                                     100    // 串通对
  );

  EXPECT_EQ(dataset.size(), 1600u);  // 1000 + 200 + 200 + 200(100对)

  // Step 2: 训练模型
  anticheat::MLEntityDetector detector;
  detector.AddTrainingData(dataset);
  detector.Train();

  ASSERT_TRUE(detector.IsTrained());
  EXPECT_GT(detector.TreeCount(), 0u);
}

TEST_F(SpectatorE2ETest, ModelExportImport) {
  anticheat::MLDataGenerator gen(42);
  auto dataset = gen.GenerateDataset(200, 40, 40, 20);

  // 训练并保存
  anticheat::MLEntityDetector detector1;
  detector1.AddTrainingData(dataset);
  detector1.Train();

  std::string path = "/tmp/poker_ml_model.bin";
  ASSERT_TRUE(detector1.SaveModel(path));

  // 加载到新实例
  anticheat::MLEntityDetector detector2;
  ASSERT_TRUE(detector2.LoadModel(path));

  std::filesystem::remove(path);
}

TEST_F(SpectatorE2ETest, TournamentSnapshotDefault) {
  auto snap = spectator_mgr_->GetSnapshot(1);
  EXPECT_EQ(snap.tournament_id, 1u);
}
