#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include "poker_engine/cfr/cfr_training.h"
#include "poker_engine/game/game_state.h"

using namespace poker_engine;
using namespace poker_engine::cfr;

class CFRIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(CFRIntegrationTest, SimpleTrainingRound) {
  CFROptions opts;
  opts.config.num_iterations = 100;
  opts.config.discount_interval = 10.0;

  CFRTrainer trainer(opts);
  auto dummy = trainer.Train(100);
  EXPECT_GT(trainer.NodeCount(), 0u);

  double exploit = trainer.EvaluateExploitability();
  std::cout << "\n100 iters exploitability: " << exploit << std::endl;

  EXPECT_TRUE(trainer.SaveModel("/tmp/cfr_test_model.cfr"));

  CFROptions opts2;
  CFRTrainer trainer2(opts2);
  EXPECT_TRUE(trainer2.LoadModel("/tmp/cfr_test_model.cfr"));
  EXPECT_GT(trainer2.NodeCount(), 0u);

  std::filesystem::remove("/tmp/cfr_test_model.cfr");
}

TEST_F(CFRIntegrationTest, RangeInteroperability) {
  HandAbstraction ab;
  Evaluator eval;
  ab.Initialize(eval);

  Range top20 = RangeBuilder(ab).TopPercent(20).Build();
  EXPECT_GT(top20.HandCount(), 0);
  EXPECT_LT(top20.HandCount(), 169 * 4);

  CFROptions opts;
  opts.config.num_iterations = 10;
  CFRTrainer trainer(opts);
  EXPECT_NO_THROW(trainer.Train(10));
}

TEST_F(CFRIntegrationTest, TrainingPerformance_1000Iters) {
  CFROptions opts;
  opts.config.num_iterations = 1000;
  opts.config.discount_interval = 50.0;
  opts.exploitability_threshold = 0.0;

  auto start = std::chrono::high_resolution_clock::now();

  CFRTrainer trainer(opts);
  trainer.Train(1000);

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::high_resolution_clock::now() - start)
                     .count();

  std::cout << "\n1000 CFR iterations: " << elapsed << "ms, "
            << "nodes=" << trainer.NodeCount() << std::endl;

  EXPECT_LT(elapsed, 30000);
}

TEST_F(CFRIntegrationTest, ModelLargeScale) {
  CFREngine engine;
  engine.Initialize();

  for (int i = 0; i < 10; ++i) {
    for (uint16_t bucket = 0; bucket < 169; ++bucket) {
      for (uint8_t street = 0; street < 4; ++street) {
        for (uint16_t pot = 0; pot < 10; ++pot) {
          for (uint8_t bs = 0; bs < 5; ++bs) {
            for (uint8_t pl = 0; pl < 2; ++pl) {
              InfosetKey key{bucket, street, pot, bs, pl};
              engine.GetOrCreateNode(key);
            }
          }
        }
      }
    }
  }

  size_t nodes = engine.NodeCount();
  std::cout << "\nCreated " << nodes << " CFR nodes" << std::endl;
  std::cout << "Estimated memory: " << (nodes * sizeof(CFRNode) / 1024 / 1024) << " MB"
            << std::endl;
  EXPECT_GT(nodes, 0u);
}
