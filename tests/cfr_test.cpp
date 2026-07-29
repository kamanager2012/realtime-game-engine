#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iostream>

#include "poker_engine/cfr/cfr_abstraction.h"
#include "poker_engine/cfr/cfr_engine.h"
#include "poker_engine/cfr/cfr_model.h"
#include "poker_engine/cfr/cfr_range.h"
#include "poker_engine/cfr/cfr_training.h"

using namespace poker_engine::cfr;

class CFRTest : public ::testing::Test {
 protected:
  void SetUp() override { engine_.Initialize(); }
  CFREngine engine_;
};

TEST_F(CFRTest, EngineInitializes) { EXPECT_GT(engine_.NodeCount(), 0u); }

TEST_F(CFRTest, HandAbstractionWorks) {
  const auto& abstraction = engine_.HandAbstraction();
  EXPECT_EQ(abstraction.get_bucket({0, 1}), abstraction.get_bucket({0, 1}));
}

TEST_F(CFRTest, HandAbstractionDifferentHands) {
  const auto& abstraction = engine_.HandAbstraction();
  uint16_t aa_bucket = abstraction.get_bucket({0, 12});
  uint16_t kk_bucket = abstraction.get_bucket({11, 24});
  EXPECT_NE(aa_bucket, kk_bucket) << "AA and KK should be in different buckets";
}

TEST_F(CFRTest, InfosetKeyHashing) {
  InfosetKey key1{50, 1, 10, 2, 0};
  InfosetKey key2{50, 1, 10, 2, 0};
  InfosetKey key3{50, 1, 10, 2, 1};
  EXPECT_EQ(key1.hash(), key2.hash());
  EXPECT_NE(key1.hash(), key3.hash());
}

TEST_F(CFRTest, RangeFullWeight) {
  auto ab = engine_.HandAbstraction();
  Range full = Range::FullRange();
  EXPECT_NEAR(full.TotalWeight(), ab.kNumBuckets, 0.01);
}

TEST_F(CFRTest, RangeOperations) {
  Range r1, r2;
  r1.SetBucket(0, 1.0);
  r1.SetBucket(1, 2.0);
  r2.SetBucket(0, 3.0);
  r2.SetBucket(1, 1.0);

  Range intersection = r1.Intersect(r2);
  EXPECT_NEAR(intersection.GetBucket(0), 1.0, 0.001);
  EXPECT_NEAR(intersection.GetBucket(1), 1.0, 0.001);

  Range diff = r1.Exclude(r2);
  EXPECT_NEAR(diff.GetBucket(0), 0.0, 0.001);
  EXPECT_NEAR(diff.GetBucket(1), 1.0, 0.001);

  Range sum = r1 + r2;
  EXPECT_NEAR(sum.GetBucket(0), 4.0, 0.001);
  EXPECT_NEAR(sum.GetBucket(1), 3.0, 0.001);

  Range scaled = r1 * 2.0;
  EXPECT_NEAR(scaled.GetBucket(0), 2.0, 0.001);
  EXPECT_NEAR(scaled.GetBucket(1), 4.0, 0.001);
}

TEST_F(CFRTest, RangeNormalization) {
  Range r;
  r.SetBucket(0, 2.0);
  r.SetBucket(1, 3.0);
  r.SetBucket(2, 5.0);
  r.Normalize();
  EXPECT_NEAR(r.TotalWeight(), 1.0, 0.001);
}

TEST_F(CFRTest, RangeFromString) {
  auto ab = engine_.HandAbstraction();
  Range r = Range::FromString("AA, KK, AKs", ab);
  EXPECT_GT(r.TotalWeight(), 0);
}

TEST_F(CFRTest, RangeBuilder) {
  auto ab = engine_.HandAbstraction();
  RangeBuilder builder(ab);
  builder.Add("AA").Add("KK+").Add("AKs");
  Range r = builder.Build();
  EXPECT_GT(r.TotalWeight(), 0);
  EXPECT_GT(r.HandCount(), 0);
}

TEST_F(CFRTest, CFRNodeInitialization) {
  CFRNode node;
  double avg[5];
  node.get_average_strategy(avg);
  for (int i = 0; i < 5; ++i) EXPECT_NEAR(avg[i], 0.2, 0.001);
}

TEST_F(CFRTest, CFRNodeRegretAccumulation) {
  CFRNode node;
  node.accumulate_regret(0, 5.0, -1000.0, 1000.0);
  node.accumulate_regret(0, 3.0, -1000.0, 1000.0);
  node.accumulate_regret(1, -2.0, -1000.0, 1000.0);
  EXPECT_DOUBLE_EQ(node.regret_sum[0], 8.0);
  EXPECT_DOUBLE_EQ(node.regret_sum[1], -2.0);
}

TEST_F(CFRTest, CFRNodeRegretClipping) {
  CFRNode node;
  node.accumulate_regret(0, 5000.0, -1000.0, 1000.0);
  EXPECT_DOUBLE_EQ(node.regret_sum[0], 1000.0);
  node.accumulate_regret(1, -5000.0, -1000.0, 1000.0);
  EXPECT_DOUBLE_EQ(node.regret_sum[1], -1000.0);
}

TEST_F(CFRTest, CFRNodeStrategyComputation) {
  CFRNode node;
  node.regret_sum[0] = 10;
  node.regret_sum[1] = 20;
  node.regret_sum[2] = 0;
  node.regret_sum[3] = 0;
  node.regret_sum[4] = -5;

  node.compute_strategy();
  EXPECT_DOUBLE_EQ(node.current_strategy[0], 10.0 / 30.0);
  EXPECT_DOUBLE_EQ(node.current_strategy[1], 20.0 / 30.0);
  EXPECT_DOUBLE_EQ(node.current_strategy[4], 0.0);
}

TEST_F(CFRTest, CFRNodeAverageStrategy) {
  CFRNode node;
  node.accumulate_strategy(0, 0.5);
  node.accumulate_strategy(1, 0.3);
  node.accumulate_strategy(0, 0.3);
  node.accumulate_strategy(1, 0.1);

  double avg[5];
  node.get_average_strategy(avg);
  EXPECT_NEAR(avg[0], 0.8 / 1.2, 0.001);
  EXPECT_NEAR(avg[1], 0.4 / 1.2, 0.001);
}

TEST_F(CFRTest, ModelSaveAndLoad) {
  std::unordered_map<uint64_t, CFRNode> test_nodes;
  CFRNode& n = test_nodes[12345];
  n.regret_sum[0] = 10.0;
  n.regret_sum[1] = 20.0;
  n.strategy_sum[0] = 5.0;
  n.strategy_sum[1] = 3.0;
  n.times_visited = 100;

  std::string path = "/tmp/test_cfr_model.cfr";
  EXPECT_TRUE(CFRModelIO::Save(path, test_nodes, 0.05));

  auto info = CFRModelIO::GetInfo(path);
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->node_count, 1u);
  EXPECT_NEAR(info->exploitability, 0.05, 0.001);

  std::unordered_map<uint64_t, CFRNode> loaded;
  EXPECT_TRUE(CFRModelIO::Load(path, loaded));
  EXPECT_EQ(loaded.size(), 1u);
  EXPECT_EQ(loaded[12345].times_visited, 100);
  EXPECT_DOUBLE_EQ(loaded[12345].regret_sum[0], 10.0);

  std::filesystem::remove(path);
}

TEST_F(CFRTest, ModelRoundTrip) {
  std::unordered_map<uint64_t, CFRNode> original;
  std::mt19937 rng(42);
  std::uniform_real_distribution<double> dist(-10.0, 10.0);

  for (int i = 0; i < 100; ++i) {
    CFRNode node;
    for (int a = 0; a < CFRNode::kMaxActions; ++a) {
      node.regret_sum[a] = dist(rng);
      node.strategy_sum[a] = std::abs(dist(rng));
    }
    node.times_visited = rng() % 1000;
    node.compute_strategy();
    original[i * 997] = node;
  }

  std::string path = "/tmp/test_cfr_roundtrip.cfr";
  ASSERT_TRUE(CFRModelIO::Save(path, original, 0.1));

  std::unordered_map<uint64_t, CFRNode> loaded;
  ASSERT_TRUE(CFRModelIO::Load(path, loaded));
  EXPECT_EQ(loaded.size(), original.size());

  for (auto& [key, orig_node] : original) {
    auto it = loaded.find(key);
    ASSERT_NE(it, loaded.end());
    for (int a = 0; a < CFRNode::kMaxActions; ++a) {
      EXPECT_DOUBLE_EQ(it->second.regret_sum[a], orig_node.regret_sum[a]);
      EXPECT_DOUBLE_EQ(it->second.strategy_sum[a], orig_node.strategy_sum[a]);
    }
    EXPECT_EQ(it->second.times_visited, orig_node.times_visited);
  }

  std::filesystem::remove(path);
}

TEST_F(CFRTest, ModelCompression) {
  std::vector<NodeRecord> nodes;
  for (uint64_t i = 0; i < 1000; ++i) {
    NodeRecord rec;
    rec.key_hash = i * 997;
    rec.regret_sum[0] = i * 0.1;
    rec.regret_sum[1] = -i * 0.05;
    rec.regret_sum[2] = i * 0.03;
    rec.regret_sum[3] = -i * 0.01;
    rec.regret_sum[4] = i * 0.07;
    rec.strategy_sum[0] = i * 0.05;
    rec.strategy_sum[1] = i * 0.03;
    rec.times_visited = i * 10;
    nodes.push_back(rec);
  }

  auto compressed = ModelCompressor::CompressNodes(nodes);
  auto decompressed = ModelCompressor::DecompressNodes(compressed);

  ASSERT_EQ(decompressed.size(), nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    EXPECT_EQ(decompressed[i].key_hash, nodes[i].key_hash);
    for (int a = 0; a < CFRNode::kMaxActions; ++a) {
      EXPECT_NEAR(decompressed[i].regret_sum[a], nodes[i].regret_sum[a], 0.01);
    }
  }

  std::cout << "\nCompression: " << nodes.size() * sizeof(NodeRecord) << " bytes -> "
            << compressed.size() << " bytes ("
            << (100.0 * compressed.size() / (nodes.size() * sizeof(NodeRecord))) << "%)"
            << std::endl;
}

TEST_F(CFRTest, SimpleTraining) {
  CFROptions opts;
  opts.config.num_iterations = 500;
  opts.config.discount_interval = 50.0;
  opts.exploitability_threshold = 1.0;

  CFREngine engine(opts);
  engine.Initialize();
  EXPECT_GT(engine.NodeCount(), 0u);

  auto ab = engine.HandAbstraction();
  InfosetKey key{0, 0, 0, 0, 0};
  auto strategy = engine.GetStrategy(key);
  EXPECT_FALSE(strategy.empty());

  double total_prob = 0;
  for (auto& [action, prob] : strategy) {
    EXPECT_GE(prob, 0.0);
    total_prob += prob;
  }
  EXPECT_NEAR(total_prob, 1.0, 0.01);
}

TEST_F(CFRTest, ConcurrentNodeAccess) {
  CFREngine engine;
  engine.Initialize();

  std::atomic<int> errors{0};

  auto worker = [&](int thread_id) {
    for (int i = 0; i < 1000; ++i) {
      InfosetKey key;
      key.hand_bucket = (i + thread_id * 100) % HandAbstraction::kNumBuckets;
      key.street = i % 4;
      key.pot_size = i % 32;
      key.bet_sequence = i % 8;
      key.player = i % 2;

      auto* node = engine.GetOrCreateNode(key);
      if (!node) errors++;
    }
  };

  std::thread t1(worker, 0);
  std::thread t2(worker, 1);
  t1.join();
  t2.join();

  EXPECT_EQ(errors.load(), 0);
}
