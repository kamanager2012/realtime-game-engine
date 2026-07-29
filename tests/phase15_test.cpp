#include <gtest/gtest.h>

#include "poker_engine/cfr/cfr_abstraction.h"
#include "poker_engine/cfr/cfr_engine.h"
#include "poker_engine/cfr/cfr_node.h"
#include "poker_engine/cfr/cfr_range.h"
#include "poker_engine/cfr/cfr_training.h"
#include "poker_engine/cfr/public_tree_solver.h"
#include "poker_engine/cfr/types.h"
#include "poker_engine/evaluator/evaluator.h"

using namespace poker_engine::cfr;

// ========== Types / Helper ==========

TEST(Phase15Test, HoleCardsEncodeDecode) {
  HoleCards hc{0, 1};  // 2c 2d
  uint16_t code = hc.encode();
  EXPECT_EQ(code, 0 * kTotalCards + 1);
  auto decoded = HoleCards::decode(code);
  EXPECT_EQ(decoded.c1, 0);
  EXPECT_EQ(decoded.c2, 1);
}

TEST(Phase15Test, ActionNames) {
  EXPECT_STREQ(action_name(Action::Fold), "fold");
  EXPECT_STREQ(action_name(Action::Call), "call");
  EXPECT_STREQ(action_name(Action::BetHalf), "half_pot");
  EXPECT_STREQ(action_name(Action::BetPot), "pot");
  EXPECT_STREQ(action_name(Action::AllIn), "all_in");
  EXPECT_EQ(action_count(), 5);
}

TEST(Phase15Test, BetHistoryEncoding) {
  BetHistory bh;
  bh.add_round_bet(1);
  bh.add_round_bet(2);
  bh.add_round_bet(3);
  EXPECT_EQ(bh.num_rounds, 3);
  uint16_t code = bh.encode();
  EXPECT_EQ(code & 0x7, 1);
  EXPECT_EQ((code >> 3) & 0x7, 2);
  EXPECT_EQ((code >> 6) & 0x7, 3);
}

// ========== CFRNode ==========

TEST(Phase15Test, CFRNodeInitialization) {
  CFRNode node;
  EXPECT_EQ(node.times_visited, 0);

  double avg[CFRNode::kMaxActions];
  node.get_average_strategy(avg);
  for (int a = 0; a < CFRNode::kMaxActions; ++a) {
    EXPECT_NEAR(avg[a], 1.0 / CFRNode::kMaxActions, 1e-9);
  }
}

TEST(Phase15Test, CFRNodeRegretAndStrategyAccumulation) {
  CFRNode node;
  node.accumulate_regret(0, 1.0, -1000.0, 1000.0);
  node.accumulate_regret(1, -0.5, -1000.0, 1000.0);
  node.compute_strategy();

  for (int a = 0; a < CFRNode::kMaxActions; ++a) {
    node.accumulate_strategy(a, node.current_strategy[a]);
  }

  double strat[CFRNode::kMaxActions];
  node.get_average_strategy(strat);
  EXPECT_NEAR(strat[0] + strat[1] + strat[2] + strat[3] + strat[4], 1.0, 1e-9);
  EXPECT_GT(strat[0], strat[1]);  // positive regret gets more weight
}

// ========== HandAbstraction ==========

TEST(Phase15Test, HandAbstractionBucketing) {
  poker_engine::evaluator::Evaluator eval;
  HandAbstraction abs;
  abs.Initialize(eval);

  // Card index = suit * 13 + rank (rank 0=2 .. 12=A; suit 0=C, 1=D, 2=H, 3=S)
  HoleCards aa{12, 25};  // Ac Ad (pair of Aces)
  uint16_t bucket_aa = abs.get_bucket(aa);
  EXPECT_LT(bucket_aa, HandAbstraction::kNumBuckets);

  HoleCards ak_suited{12, 11};  // Ac Kc (suited)
  uint16_t bucket_ak = abs.get_bucket(ak_suited);
  EXPECT_LT(bucket_ak, HandAbstraction::kNumBuckets);

  EXPECT_NE(bucket_aa, bucket_ak);
  EXPECT_EQ(abs.bucket_name(bucket_aa), "AA");
  EXPECT_EQ(abs.bucket_name(bucket_ak), "AKs");
}

TEST(Phase15Test, InfosetKeyHashAndEquality) {
  InfosetKey k1{1, 2, 100, 3, 0};
  InfosetKey k2{1, 2, 100, 3, 0};
  InfosetKey k3{2, 2, 100, 3, 0};

  EXPECT_EQ(k1.hash(), k2.hash());
  EXPECT_TRUE(k1 == k2);
  EXPECT_NE(k1.hash(), k3.hash());
}

// ========== Range ==========

TEST(Phase15Test, RangeFullRange) {
  poker_engine::evaluator::Evaluator eval;
  HandAbstraction abs;
  abs.Initialize(eval);

  Range r = Range::FullRange();
  EXPECT_GT(r.TotalWeight(), 0.0);
  // Range is bucket-based; FullRange sets every bucket weight to 1.
  EXPECT_NEAR(r.TotalWeight(), static_cast<double>(Range::kBuckets), 1e-9);
}

TEST(Phase15Test, RangeFromStringAndOperations) {
  poker_engine::evaluator::Evaluator eval;
  HandAbstraction abs;
  abs.Initialize(eval);

  Range r1 = Range::FromString("AA", abs);
  EXPECT_NEAR(r1.TotalWeight(), 1.0, 1e-9);  // one bucket weight

  Range r2 = Range::FromString("KK", abs);
  Range r_union = r1.Union(r2);
  EXPECT_GT(r_union.TotalWeight(), r1.TotalWeight());

  Range r_inter = r_union.Intersect(r1);
  EXPECT_NEAR(r_inter.TotalWeight(), r1.TotalWeight(), 1e-9);
}

TEST(Phase15Test, RangeBuilder) {
  poker_engine::evaluator::Evaluator eval;
  HandAbstraction abs;
  abs.Initialize(eval);

  Range r = RangeBuilder(abs).Add("AA").Add("KK").Add("AKs").Build();

  EXPECT_GT(r.TotalWeight(), 0.0);
  EXPECT_NEAR(r.TotalWeight(), 3.0, 1e-9);  // three buckets
}

// ========== PublicTree ==========

TEST(Phase15Test, PublicTreeBuildSmall) {
  PublicTree tree;
  tree.Build(2, 1);  // max_street=Flop, max_bets_per_round=1
  EXPECT_GT(tree.NodeCount(), 0u);
  EXPECT_EQ(tree.Root()->street, Street::Preflop);
  EXPECT_GT(tree.TerminalCount(), 0u);
}

// ========== CFREngine / Trainer Construction ==========

TEST(Phase15Test, CFREngineInitialize) {
  CFROptions options;
  options.config.num_iterations = 1;
  options.check_interval = 1;

  CFREngine engine(options);
  engine.Initialize();

  EXPECT_EQ(engine.NodeCount(), 0u);  // Nodes are created during training, not init
  EXPECT_EQ(engine.Nodes().size(), 0u);
}

TEST(Phase15Test, CFRTrainerConfiguration) {
  CFRTrainer trainer;
  trainer.SetIterations(10);
  trainer.SetDiscountInterval(2.0);
  trainer.SetExploitabilityThreshold(1.0);
  trainer.EnablePruning(false);

  EXPECT_EQ(trainer.Engine().Options().config.num_iterations, 10);
  EXPECT_EQ(trainer.Engine().Options().enable_pruning, false);
}

TEST(Phase15Test, PublicTreeSolverConstruction) {
  SolverOptions options;
  options.num_iterations = 1;
  options.check_interval = 1;

  PublicTreeSolver solver(options);
  EXPECT_EQ(solver.IterationCount(), 0u);
}
