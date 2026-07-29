#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "poker_engine/cfr/cfr_engine.h"
#include "poker_engine/cfr/cfr_model.h"
#include "poker_engine/cfr/gpu_batcher.h"
#include "poker_engine/cfr/parallel_cfr.h"
#include "poker_engine/cfr/public_tree_solver.h"
#include "poker_engine/economy/elo_rating.h"

using namespace poker_engine::cfr;
using namespace poker_engine::economy;

// ==================== Parallel Training Performance ====================

TEST(CFRPerfTest, ParallelTraining_10KBatch) {
  ParallelCFROptions opts;
  opts.num_threads = 2;
  opts.batch_size = 1024;
  opts.sync_interval = 50;

  ParallelCFRTrainer trainer(opts);
  trainer.Initialize();

  auto start = std::chrono::steady_clock::now();
  trainer.Train(10000);
  auto elapsed = std::chrono::steady_clock::now() - start;

  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  auto stats = trainer.GetStats();

  std::cout << "\n  Parallel 10K training: " << ms << "ms, " << stats.samples_per_second
            << " samples/s, " << stats.total_nodes << " nodes" << std::endl;

  EXPECT_GT(stats.iterations_completed, 0);
  EXPECT_GT(stats.total_nodes, 0u);
  EXPECT_GT(stats.elapsed_ms, 0.0);

  double throughput = stats.samples_per_second;
  EXPECT_GT(throughput, 0.0) << "Throughput should be positive, got " << throughput;
}

// ==================== Model IO Performance ====================

TEST(CFRPerfTest, ModelIO_WithLargeModel) {
  std::unordered_map<uint64_t, CFRNode> nodes;
  std::mt19937 rng(42);
  std::uniform_real_distribution<double> dist(-10.0, 10.0);

  const int kNodeCount = 50000;
  for (int i = 0; i < kNodeCount; ++i) {
    CFRNode node;
    for (int a = 0; a < CFRNode::kMaxActions; ++a) {
      node.regret_sum[a] = dist(rng);
      node.strategy_sum[a] = std::abs(dist(rng));
    }
    node.times_visited = rng() % 10000;
    node.compute_strategy();
    nodes[i * 997 + 1] = node;
  }

  std::string path = "/tmp/test_cfr_perf_model.cfr";

  auto save_start = std::chrono::steady_clock::now();
  ASSERT_TRUE(CFRModelIO::Save(path, nodes, 0.05));
  auto save_end = std::chrono::steady_clock::now();
  auto save_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(save_end - save_start).count();

  std::unordered_map<uint64_t, CFRNode> loaded;
  auto load_start = std::chrono::steady_clock::now();
  ASSERT_TRUE(CFRModelIO::Load(path, loaded));
  auto load_end = std::chrono::steady_clock::now();
  auto load_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(load_end - load_start).count();

  EXPECT_EQ(loaded.size(), nodes.size());

  std::cout << "\n  Model IO (50K nodes): save=" << save_ms << "ms, load=" << load_ms << "ms"
            << std::endl;

  std::filesystem::remove(path);
}

// ==================== Public Tree Build Performance ====================

TEST(CFRPerfTest, PublicTreeBuild) {
  auto start = std::chrono::steady_clock::now();

  PublicTree tree;
  tree.Build(static_cast<int>(Street::River), 2);

  auto elapsed = std::chrono::steady_clock::now() - start;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  size_t node_count = tree.NodeCount();
  size_t terminal_count = tree.TerminalCount();

  std::cout << "\n  PublicTree build: " << ms << "ms, " << node_count << " nodes, "
            << terminal_count << " terminal" << std::endl;

  EXPECT_GT(node_count, 0u);
  EXPECT_GT(terminal_count, 0u);
  EXPECT_LT(node_count, 100000u) << "Tree should be bounded";
}

// ==================== Public Tree Solver ====================

TEST(CFRPerfTest, PublicTreeSolver) {
  PublicTree tree;
  tree.Build(static_cast<int>(Street::River), 2);

  SolverOptions opts;
  opts.num_iterations = 100;
  opts.check_interval = 50;
  opts.target_exploitability = 0.001;
  opts.use_cfr_plus = true;

  PublicTreeSolver solver(opts);
  solver.Initialize(tree);

  auto start = std::chrono::steady_clock::now();
  solver.Solve(100);
  auto elapsed = std::chrono::steady_clock::now() - start;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  double exploit = solver.CurrentExploitability();

  std::cout << "\n  PublicTreeSolver 100 iters: " << ms << "ms, exploitability=" << exploit
            << std::endl;

  EXPECT_GE(exploit, 0.0);
  EXPECT_EQ(solver.IterationCount(), 100u);
}

// ==================== Elo Basic Performance ====================

TEST(CFRPerfTest, EloBasic) {
  const int kDuels = 100000;
  double rating_a = 1500.0;
  double rating_b = 1500.0;

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < kDuels; ++i) {
    auto result = EloRating::ProcessDuel(rating_a, rating_b, 1.0, 0.0);
    rating_a = result.rating_a_after;
    rating_b = result.rating_b_after;
  }

  auto elapsed = std::chrono::steady_clock::now() - start;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  std::cout << "\n  Elo " << kDuels << " duels: " << ms << "ms, final ratings: " << rating_a
            << " / " << rating_b << std::endl;

  EXPECT_GT(rating_a, 1500.0);
  EXPECT_LT(rating_b, 1500.0);
}

// ==================== Elo Expected Score ====================

TEST(CFRPerfTest, EloExpectedScore) {
  double expected_400 = EloRating::ExpectedScore(1900.0, 1500.0);
  double expected_800 = EloRating::ExpectedScore(2300.0, 1500.0);
  double expected_0 = EloRating::ExpectedScore(1500.0, 1500.0);

  std::cout << "\n  Elo expected score (400 diff): " << expected_400 << " (~0.91)" << std::endl;
  std::cout << "  Elo expected score (800 diff): " << expected_800 << " (~0.99)" << std::endl;
  std::cout << "  Elo expected score (0 diff):   " << expected_0 << " (0.50)" << std::endl;

  EXPECT_NEAR(expected_0, 0.5, 0.01);
  EXPECT_GT(expected_400, 0.85);
  EXPECT_GT(expected_800, 0.97);

  double ratio_400 = expected_400 / (1.0 - expected_400);
  EXPECT_GT(ratio_400, 8.0) << "400 point diff should produce ~10:1 odds, got " << ratio_400
                            << ":1";
}

// ==================== GPU Batcher (CPU fallback) ====================

TEST(CFRPerfTest, GPUBatcher_CPUFallback) {
  GPUBatcher batcher;

  std::unordered_map<uint64_t, CFRNode> nodes;
  CFRNode& n = nodes[0];
  n.regret_sum[0] = 5.0;
  n.regret_sum[1] = 10.0;
  n.strategy_sum[0] = 3.0;
  n.strategy_sum[1] = 7.0;
  n.compute_strategy();

  batcher.UploadNodes(nodes);

  std::vector<BatchInput> inputs(1000);
  for (int i = 0; i < 1000; ++i) {
    inputs[i].hand_bucket = 0;
    inputs[i].street = 0;
    inputs[i].pot_size = 0;
    inputs[i].bet_sequence = 0;
  }

  std::vector<BatchOutput> outputs;
  auto start = std::chrono::steady_clock::now();
  batcher.PredictBatch(inputs, outputs);
  auto elapsed = std::chrono::steady_clock::now() - start;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  EXPECT_EQ(outputs.size(), 1000u);

  for (auto& out : outputs) {
    double total = 0.0;
    for (int a = 0; a < kMaxActions; ++a) total += out.strategy[a];
    EXPECT_NEAR(total, 1.0, 0.01);
  }

  std::cout << "\n  GPUBatcher (CPU fallback) 1K predictions: " << ms << "ms" << std::endl;
}
