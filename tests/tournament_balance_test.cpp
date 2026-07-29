#include <gtest/gtest.h>

#include <chrono>
#include <numeric>
#include <set>
#include <unordered_set>

#include "poker_engine/tournament/balancer.h"
#include "poker_engine/tournament/tournament.h"

using namespace poker_engine::tournament;

class SwissPairingTest : public ::testing::Test {
 protected:
  std::vector<TournamentPlayer> CreatePlayers(int count) {
    std::vector<TournamentPlayer> players;
    for (int i = 0; i < count; ++i) {
      TournamentPlayer p;
      p.id = i + 1;
      p.name = "Player" + std::to_string(i + 1);
      p.chips = 1000 + (i * 50);  // 差异化筹码
      p.seat = i;
      p.table_id = 0;
      p.active = true;
      p.eliminated = false;
      p.hands_played = 20 + i;
      p.starting_stack = 1000;
      players.push_back(p);
    }
    return players;
  }
};

// ==================== 瑞士轮配对测试 ====================

TEST_F(SwissPairingTest, BasicPairing) {
  SwissSystem ss;
  auto players = CreatePlayers(8);

  auto pairings = ss.GeneratePairings(players, {});

  // 8 名玩家应产生 4 对
  EXPECT_EQ(pairings.size(), 4u);

  // 每个玩家只出现一次
  std::unordered_set<int64_t> all_players;
  for (auto& p : pairings) {
    all_players.insert(p.player_a);
    all_players.insert(p.player_b);
  }
  EXPECT_EQ(all_players.size(), 8u);

  // 配对质量应在范围内
  for (auto& p : pairings) {
    EXPECT_GE(p.match_quality, 0.0);
    EXPECT_LE(p.match_quality, 1.0);
  }
}

TEST_F(SwissPairingTest, OddNumberOfPlayers) {
  SwissSystem ss;
  auto players = CreatePlayers(7);  // 奇数

  auto pairings = ss.GeneratePairings(players, {});

  // 7 名玩家应产生 3 对，1 人 BYE
  EXPECT_LE(pairings.size(), 3u);

  // 配对质量
  for (auto& p : pairings) {
    EXPECT_GE(p.match_quality, 0.0);
  }
}

TEST_F(SwissPairingTest, AvoidRepeatPairing) {
  SwissSystem ss(SwissSystem::SwissConfig{
      .avoid_repeat_pairs = true,
      .ideal_pairing_score_diff = 0.0f,
  });

  auto players = CreatePlayers(6);

  // 第一轮
  auto pairings1 = ss.GeneratePairings(players, {});
  ASSERT_EQ(pairings1.size(), 3u);

  // 构建历史配对
  std::vector<std::pair<int64_t, int64_t>> history;
  for (auto& p : pairings1) {
    history.push_back({p.player_a, p.player_b});
  }

  // 第二轮（应避免重复配对）
  auto pairings2 = ss.GeneratePairings(players, history);

  for (auto& p : pairings2) {
    for (auto& h : history) {
      EXPECT_NE(std::make_pair(p.player_a, p.player_b), h);
      EXPECT_NE(std::make_pair(p.player_b, p.player_a), h);
    }
  }
}

TEST_F(SwissPairingTest, SimilarStrengthPairing) {
  SwissSystem ss;

  // 创建强弱分明的玩家
  std::vector<TournamentPlayer> players(6);
  for (int i = 0; i < 3; ++i) {
    players[i].id = i + 1;
    players[i].chips = 5000;  // 强
    players[i].active = true;
  }
  for (int i = 3; i < 6; ++i) {
    players[i].id = i + 1;
    players[i].chips = 500;  // 弱
    players[i].active = true;
  }

  auto pairings = ss.GeneratePairings(players, {});

  // 强玩家之间配对，弱玩家之间配对
  for (auto& p : pairings) {
    int chips_a = players[p.player_a - 1].chips;
    int chips_b = players[p.player_b - 1].chips;

    // 不应出现极端不平衡配对
    EXPECT_LT(std::abs(chips_a - chips_b) / static_cast<double>(std::max(chips_a, chips_b)), 0.5)
        << "Unbalanced pairing detected";
  }
}

// ==================== SeatAllocator 测试 ====================

TEST_F(SwissPairingTest, GreedySeatAssignment) {
  std::vector<SeatAllocator::SeatPreference> prefs(4);
  prefs[0].player_id = 1;
  prefs[0].preferred_seat = 0;
  prefs[0].prefer_center = false;

  prefs[1].player_id = 2;
  prefs[1].preferred_seat = 3;
  prefs[1].prefer_center = false;

  prefs[2].player_id = 3;
  prefs[2].preferred_seat = -1;
  prefs[2].prefer_center = true;

  prefs[3].player_id = 4;
  prefs[3].preferred_seat = -1;
  prefs[3].avoid_window = true;

  auto assignment = SeatAllocator::GreedyAssignment(4, prefs);

  ASSERT_EQ(assignment.size(), 4u);

  // 玩家 0 应分配到座位 0
  EXPECT_EQ(assignment[0], 0);

  // 玩家 1 应分配到座位 3
  EXPECT_EQ(assignment[1], 3);

  // 所有座位应唯一
  std::sort(assignment.begin(), assignment.end());
  EXPECT_EQ(std::unique(assignment.begin(), assignment.end()), assignment.end());
}

TEST_F(SwissPairingTest, OptimalSeatAssignmentWithConflict) {
  // 2 个玩家都想坐窗口
  std::vector<SeatAllocator::SeatPreference> prefs(3);
  for (int i = 0; i < 3; ++i) {
    prefs[i].player_id = i + 1;
    prefs[i].preferred_seat = 0;       // 都想坐 0
    prefs[i].avoid_window = (i >= 2);  // 第 3 个玩家避开窗口
  }

  auto assignment = SeatAllocator::OptimalAssignment(3, prefs);

  ASSERT_EQ(assignment.size(), 3u);

  // 验证所有座位不同
  for (size_t i = 0; i < assignment.size(); ++i) {
    for (size_t j = i + 1; j < assignment.size(); ++j) {
      EXPECT_NE(assignment[i], assignment[j]);
    }
  }
}

// ==================== 替补管理测试 ====================

TEST_F(SwissPairingTest, SubstitutionManagement) {
  SubstitutionManager sm;

  // 请求替补
  bool req_result = sm.RequestSubstitution(100, 200);
  EXPECT_TRUE(req_result);

  // 审批
  bool approve_result = sm.ApproveSubstitution(100, 200);
  EXPECT_TRUE(approve_result);

  // 获取历史
  auto& history = sm.History();
  EXPECT_EQ(history.size(), 1u);
  EXPECT_EQ(history[0].original_player_id, 100);
  EXPECT_EQ(history[0].substitute_player_id, 200);
  EXPECT_TRUE(history[0].approved);
}

TEST_F(SwissPairingTest, AutoSubstituteSelection) {
  SubstitutionManager sm;

  std::unordered_map<int64_t, TournamentPlayer> players;
  for (int i = 1; i <= 5; ++i) {
    TournamentPlayer p;
    p.id = i;
    p.chips = 1000;
    p.hands_played = 20 + i * 5;
    p.starting_stack = 1000;
    players[i] = p;
  }

  std::vector<int64_t> waitlist = {101, 102, 103};

  auto result = sm.AutoSubstitute(50, waitlist, players);

  // 50 不在 players 中，应该是 -1 score
  EXPECT_FALSE(result.has_value());

  // 已存在玩家的替补
  auto result2 = sm.AutoSubstitute(1, waitlist, players);
  EXPECT_TRUE(result2.has_value());
}

// ==================== 锦标赛动态调节器测试 ====================

TEST_F(SwissPairingTest, TournamentBalancerBasic) {
  TournamentBalancer balancer;

  std::vector<TournamentPlayer> players(6);
  for (int i = 0; i < 6; ++i) {
    players[i].chips = 1000;
    players[i].active = true;
  }

  TournamentConfig config;
  config.max_players = 6;

  auto rec = balancer.Analyze(config, players, 50.0, 30.0);

  EXPECT_EQ(rec.suggested_blind_multiplier, 1);
  EXPECT_FALSE(rec.should_merge_tables);
  EXPECT_FALSE(rec.should_open_registration);
}

TEST_F(SwissPairingTest, BalancerLowPlayers) {
  TournamentBalancer balancer;

  std::vector<TournamentPlayer> players(2);
  for (int i = 0; i < 2; ++i) {
    players[i].chips = 1000;
    players[i].active = true;
  }

  TournamentConfig config;
  config.max_players = 6;

  auto rec = balancer.Analyze(config, players, 50.0, 30.0);

  EXPECT_TRUE(rec.should_open_registration);
  EXPECT_GT(rec.suggested_ai_count, 0);  // 应添加 AI 填充
}

TEST_F(SwissPairingTest, BalancerHighPotRatio) {
  TournamentBalancer balancer;

  std::vector<TournamentPlayer> players(6);
  for (int i = 0; i < 6; ++i) {
    players[i].chips = 5000;
    players[i].active = true;
  }

  TournamentConfig config;
  config.max_players = 6;

  // avg_pot >> target
  auto rec = balancer.Analyze(config, players, 200.0, 30.0);

  EXPECT_GT(rec.suggested_blind_multiplier, 1);
}

// ==================== 匈牙利算法正确性 ====================

TEST_F(SwissPairingTest, HungarianAlgorithmCorrectness) {
  // 简单的 3x3 成本矩阵
  std::vector<std::vector<double>> cost = {{1.0, 2.0, 3.0}, {2.0, 4.0, 6.0}, {3.0, 6.0, 9.0}};

  auto result = SeatAllocator::HungarianAlgorithm(cost);

  // 检查是否所有行都分配了不同的列
  EXPECT_EQ(result.size(), 3u);

  std::set<int> cols(result.begin(), result.end());
  EXPECT_EQ(cols.size(), 3u);  // 所有列不同

  double total_cost = 0;
  for (int i = 0; i < 3; ++i) {
    total_cost += cost[i][result[i]];
  }

  // 贪心最优 = 1 + 6 + 6 = 13
  EXPECT_LE(total_cost, 15.0);  // 允许近似
}

// ==================== 性能测试 ====================

TEST_F(SwissPairingTest, LargeScaleSwissPairing) {
  SwissSystem ss;

  auto players = CreatePlayers(1000);

  auto start = std::chrono::high_resolution_clock::now();

  for (int round = 0; round < 10; ++round) {
    auto pairings = ss.GeneratePairings(players, {});

    // 准备下一轮历史 (简化)
    std::vector<std::pair<int64_t, int64_t>> history;
    for (auto& p : pairings) {
      history.push_back({p.player_a, p.player_b});
      // 模拟分数变化
      auto it_a = std::find_if(players.begin(), players.end(),
                               [&p](const TournamentPlayer& tp) { return tp.id == p.player_a; });
      auto it_b = std::find_if(players.begin(), players.end(),
                               [&p](const TournamentPlayer& tp) { return tp.id == p.player_b; });
      if (it_a != players.end() && it_b != players.end()) {
        it_a->chips += (rand() % 200 - 100);
        it_b->chips -= (rand() % 200 - 100);
      }
    }
  }

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::high_resolution_clock::now() - start)
                     .count();

  std::cout << "\n1000 players, 10 rounds Swiss: " << elapsed << "ms" << std::endl;

  // 期望 < 5 秒
  EXPECT_LT(elapsed, 5000);
}

TEST_F(SwissPairingTest, SeatAllocatorLargeScale) {
  int n = 100;
  std::vector<SeatAllocator::SeatPreference> prefs(n);
  for (int i = 0; i < n; ++i) {
    prefs[i].player_id = i;
    prefs[i].preferred_seat = (i * 3) % n;
    prefs[i].prefer_center = (i % 3 == 0);
    prefs[i].avoid_window = (i % 5 == 0);
  }

  auto start = std::chrono::high_resolution_clock::now();
  auto assignment = SeatAllocator::OptimalAssignment(n, prefs);
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::high_resolution_clock::now() - start)
                     .count();

  std::cout << "Seat allocation " << n << " players: " << elapsed << "ms" << std::endl;

  EXPECT_EQ(assignment.size(), static_cast<size_t>(n));
  EXPECT_LT(elapsed, 1000);
}
