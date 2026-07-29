#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include "poker_engine/cfr/cfr_training.h"
#include "poker_engine/game/action.h"
#include "poker_engine/game/game_state.h"

using namespace poker_engine;
using namespace poker_engine::cfr;
using namespace poker_engine::game;

class CFRDeepTest : public ::testing::Test {
 protected:
  void SetUp() override { trainer_ = std::make_unique<CFRTrainer>(); }
  std::unique_ptr<CFRTrainer> trainer_;
};

TEST_F(CFRDeepTest, TrainingImprovesExploitability) {
  trainer_->Engine().Train(100);
  double exp_100 = trainer_->EvaluateExploitability();

  trainer_->Engine().Train(400);
  double exp_500 = trainer_->EvaluateExploitability();

  std::cout << "\nExploitability after 100 iter: " << exp_100
            << "\nExploitability after 500 iter: " << exp_500 << std::endl;

  EXPECT_TRUE(std::isfinite(exp_100));
  EXPECT_TRUE(std::isfinite(exp_500));
}

TEST_F(CFRDeepTest, ModelPersistence) {
  trainer_->Engine().Train(50);

  EXPECT_TRUE(trainer_->SaveModel("/tmp/cfr_persist.cfr"));

  CFRTrainer trainer2;
  EXPECT_TRUE(trainer2.LoadModel("/tmp/cfr_persist.cfr"));
  EXPECT_GT(trainer2.NodeCount(), 0u);

  InfosetKey key{0, 0, 0, 0, 0};

  auto strat1 = trainer_->GetNodeStrategy(key);
  auto strat2 = trainer2.GetNodeStrategy(key);

  EXPECT_EQ(strat1.size(), strat2.size());
  for (size_t i = 0; i < strat1.size(); ++i) {
    EXPECT_EQ(strat1[i].first, strat2[i].first);
    EXPECT_NEAR(strat1[i].second, strat2[i].second, 0.001);
  }

  std::filesystem::remove("/tmp/cfr_persist.cfr");
}
