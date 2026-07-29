#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include "poker_engine/base/serialization.h"
#include "poker_engine/game/action_validator.h"
#include "poker_engine/game/table.h"
#include "poker_engine/network/ai_engine.h"
#include "poker_engine/network/session_manager.h"

using namespace poker_engine;
using namespace std::chrono;

// ==================== 统计工具 ====================

struct BenchmarkStats {
  std::vector<int64_t> samples_us;

  void add(int64_t us) { samples_us.push_back(us); }

  void print(const std::string& name) {
    if (samples_us.empty()) return;
    std::sort(samples_us.begin(), samples_us.end());

    double mean = static_cast<double>(std::accumulate(samples_us.begin(), samples_us.end(), 0LL)) /
                  samples_us.size();

    int64_t p50 = samples_us[static_cast<size_t>(samples_us.size() * 0.50)];
    int64_t p95 = samples_us[static_cast<size_t>(samples_us.size() * 0.95)];
    int64_t p99 = samples_us[static_cast<size_t>(samples_us.size() * 0.99)];
    int64_t min_ = samples_us.front();
    int64_t max_ = samples_us.back();

    std::cout << "\n┌─────────────────────────────────────────────────┐\n";
    std::cout << "│ " << std::left << std::setw(43) << name << "│\n";
    std::cout << "├─────────────────────────────────────────────────┤\n";
    std::cout << "│ samples: " << std::setw(33) << samples_us.size() << "│\n";
    std::cout << "│ mean:    " << std::setw(33) << std::fixed << std::setprecision(1) << mean
              << " µs│\n";
    std::cout << "│ p50:     " << std::setw(33) << p50 << " µs│\n";
    std::cout << "│ p95:     " << std::setw(33) << p95 << " µs│\n";
    std::cout << "│ p99:     " << std::setw(33) << p99 << " µs│\n";
    std::cout << "│ min:     " << std::setw(33) << min_ << " µs│\n";
    std::cout << "│ max:     " << std::setw(33) << max_ << " µs│\n";
    std::cout << "└─────────────────────────────────────────────────┘\n";

    auto check = [&](int64_t actual, int64_t threshold, const char* label) {
      if (actual <= threshold) {
        std::cout << "  ✅ " << label << ": " << actual << " µs ≤ " << threshold << " µs\n";
      } else {
        std::cout << "  ❌ " << label << ": " << actual << " µs > " << threshold << " µs\n";
      }
    };

    check(p50, 100000, "p50 < 100ms");
    check(p99, 1000000, "p99 < 1s");
  }
};

// ==================== 测试夹具 ====================

class PerformanceBenchmark : public ::testing::Test {
 protected:
  std::mt19937 rng_{std::random_device{}()};

  int32_t randomRank() { return std::uniform_int_distribution<int>(0, 12)(rng_); }
  int32_t randomSuit() { return std::uniform_int_distribution<int>(0, 3)(rng_); }

  std::vector<int32_t> randomHoleCards() {
    return {randomSuit() * 13 + randomRank(), randomSuit() * 13 + randomRank()};
  }
};

// ==================== BENCHMARK 1: AI 决策延迟 ====================

TEST_F(PerformanceBenchmark, AI_DecisionLatency_10000) {
  ai::AIConfig config;
  config.strategy_type = "rule_based";
  config.aggression = 1.0f;
  config.tightness = 1.0f;
  config.time_limit_ms = 5000;

  auto ai = ai::CreateAIEngine(config);

  game::GameState state;
  state.table_id = "bench";
  state.status = game::GameStatus::Playing;
  state.phase = game::GamePhase::Flop;
  state.current_bet = 10;
  state.min_raise_size = 4;
  state.pot = 50;
  state.dealer_seat = 0;

  state.players.resize(6);
  for (int i = 0; i < 6; ++i) {
    state.players[i].player_id = 100 + i;
    state.players[i].seat_index = static_cast<uint8_t>(i);
    state.players[i].chips = 1000;
    state.players[i].status = game::PlayerStatus::Active;
    state.players[i].action_status = game::ActionStatus::None;
    state.players[i].occupied = true;
  }

  ai::DecisionRequest request;
  request.state = &state;
  request.player_id = 100;
  request.legal_actions = {
      {game::ActionType::Fold, 0}, {game::ActionType::Call, 10}, {game::ActionType::Raise, 20}};

  BenchmarkStats stats;
  const int N = 10000;

  for (int i = 0; i < N; ++i) {
    state.players[0].hole_cards = randomHoleCards();
    auto start = high_resolution_clock::now();
    auto resp = ai->Decide(request);
    auto elapsed = duration_cast<microseconds>(high_resolution_clock::now() - start);
    stats.add(elapsed.count());
  }

  stats.print("AI Decision Latency (10K samples, postflop)");

  std::sort(stats.samples_us.begin(), stats.samples_us.end());
  int64_t p99 = stats.samples_us[static_cast<size_t>(N * 0.99)];
  EXPECT_LT(p99, 100000) << "AI decision p99 latency should be < 100ms";
}

// ==================== BENCHMARK 2: ActionValidator 吞吐 ====================

TEST_F(PerformanceBenchmark, ActionValidator_Throughput_100000) {
  game::ActionValidator validator;

  game::GameState state;
  state.status = game::GameStatus::Playing;
  state.phase = game::GamePhase::Flop;
  state.current_bet = 10;
  state.min_raise_size = 4;
  state.pot = 50;
  state.dealer_seat = 0;

  state.players.resize(6);
  for (int i = 0; i < 6; ++i) {
    state.players[i].player_id = 100 + i;
    state.players[i].seat_index = static_cast<uint8_t>(i);
    state.players[i].chips = 1000;
    state.players[i].status = game::PlayerStatus::Active;
    state.players[i].action_status = game::ActionStatus::None;
    state.players[i].bet_this_round = 0;
    state.players[i].occupied = true;
  }
  state.players[0].action_status = game::ActionStatus::None;
  state.current_player_id = 100;

  BenchmarkStats stats;
  const int N = 100000;

  for (int i = 0; i < N; ++i) {
    auto start = high_resolution_clock::now();
    auto result = validator.Validate(state, 100, game::ActionType::Call, 10);
    auto elapsed = duration_cast<nanoseconds>(high_resolution_clock::now() - start);
    stats.add(elapsed.count() / 1000);
  }

  stats.print("ActionValidator Throughput (100K validations)");

  std::sort(stats.samples_us.begin(), stats.samples_us.end());
  int64_t p50 = stats.samples_us[static_cast<size_t>(N * 0.50)];
  EXPECT_LT(p50, 100) << "ActionValidator p50 should be < 100µs";
}

// ==================== BENCHMARK 3: 序列化/反序列化 ====================

TEST_F(PerformanceBenchmark, Serialization_RoundTrip_50000) {
  BenchmarkStats stats;
  const int N = 50000;

  for (int i = 0; i < N; ++i) {
    auto msg = base::MessageBuilder::ActionTaken(i, "player_100", "raise", 50, 950);

    auto start = high_resolution_clock::now();
    auto serialized = msg.Serialize();
    auto parsed = base::WSMessage::Deserialize(serialized);
    auto elapsed = duration_cast<microseconds>(high_resolution_clock::now() - start);
    stats.add(elapsed.count());

    ASSERT_TRUE(parsed.has_value());
  }

  stats.print("Serialization RoundTrip (50K messages)");

  std::sort(stats.samples_us.begin(), stats.samples_us.end());
  int64_t p99 = stats.samples_us[static_cast<size_t>(N * 0.99)];
  EXPECT_LT(p99, 1000) << "Serialization p99 should be < 1ms";
}

// ==================== BENCHMARK 4: Session 管理 ====================

TEST_F(PerformanceBenchmark, Session_CreateAndQuery_50000) {
  network::SessionManager sessions(std::chrono::seconds(60));

  BenchmarkStats stats;
  const int N = 50000;

  // 创建
  auto start = high_resolution_clock::now();
  for (int i = 0; i < N; ++i) {
    sessions.Create(1000 + i, "table_" + std::to_string(i % 10));
  }
  auto elapsed = duration_cast<microseconds>(high_resolution_clock::now() - start);
  stats.add(elapsed.count());

  stats.print("Session Create (50K sessions)");

  // 查询
  BenchmarkStats query_stats;
  start = high_resolution_clock::now();
  for (int i = 0; i < N; ++i) {
    sessions.Get("test_token_" + std::to_string(i));
  }
  elapsed = duration_cast<microseconds>(high_resolution_clock::now() - start);
  query_stats.add(elapsed.count());
  query_stats.print("Session Get (50K lookups - miss)");
}

// ==================== BENCHMARK 5: 完整手牌模拟吞吐 ====================

TEST_F(PerformanceBenchmark, FullHandSimulation_1000) {
  game::TableConfig config;
  config.table_id = "bench_table";
  config.max_players = 6;
  config.small_blind = 1;
  config.big_blind = 2;
  config.min_buy_in = 20;
  config.max_buy_in = 200;

  int completed_hands = 0;

  BenchmarkStats stats;
  const int N = 1000;

  for (int h = 0; h < N; ++h) {
    game::Table table(config);

    for (int i = 0; i < 6; ++i) {
      table.SitDown(100 + i, static_cast<uint8_t>(i), 100);
    }

    auto start = high_resolution_clock::now();
    table.StartHand();

    auto& state = table.GetState();

    for (int attempt = 0; attempt < 20; ++attempt) {
      if (state.status != game::GameStatus::Playing) break;

      auto* player = state.MutablePlayer(state.current_player_id);
      if (!player || !player->occupied || player->has_folded()) {
        break;
      }

      bool should_play = (attempt < 2);
      if (should_play) {
        if (state.current_bet > 0) {
          table.ProcessAction(state.current_player_id, game::ActionType::Call);
        } else {
          table.ProcessAction(state.current_player_id, game::ActionType::Check);
        }
      } else {
        table.ProcessAction(state.current_player_id, game::ActionType::Fold);
      }
    }

    auto elapsed = duration_cast<microseconds>(high_resolution_clock::now() - start);
    stats.add(elapsed.count());
    completed_hands++;
  }

  stats.print("Full Hand Simulation (1K hands, 6 players)");
  std::cout << "  Completed hands: " << completed_hands << "\n";

  std::sort(stats.samples_us.begin(), stats.samples_us.end());
  double hands_per_second = 1000000.0 / stats.samples_us[stats.samples_us.size() / 2];
  std::cout << "  Throughput: ~" << std::fixed << std::setprecision(1) << hands_per_second
            << " hands/sec\n";
}

// ==================== BENCHMARK 6: 内存基线 ====================

TEST_F(PerformanceBenchmark, MemoryBaseline) {
  size_t table_size = sizeof(game::Table);
  size_t state_size = sizeof(game::GameState);
  size_t player_size = sizeof(game::PlayerState);
  size_t action_size = sizeof(game::Action);
  size_t ws_msg_size = sizeof(base::WSMessage);
  size_t session_size = sizeof(network::Session);

  std::cout << "\n┌─────────────────────────────────────┐\n";
  std::cout << "│ Memory Layout (sizeof)               │\n";
  std::cout << "├─────────────────────────────────────┤\n";
  std::cout << "│ Table:        " << std::setw(6) << table_size << " bytes │" << "\n";
  std::cout << "│ GameState:    " << std::setw(6) << state_size << " bytes │" << "\n";
  std::cout << "│ PlayerState:  " << std::setw(6) << player_size << " bytes │" << "\n";
  std::cout << "│ Action:       " << std::setw(6) << action_size << " bytes │" << "\n";
  std::cout << "│ WSMessage:    " << std::setw(6) << ws_msg_size << " bytes │" << "\n";
  std::cout << "│ Session:      " << std::setw(6) << session_size << " bytes │" << "\n";
  std::cout << "└─────────────────────────────────────┘\n";

  size_t per_table = state_size + 6 * player_size + 10 * action_size;
  size_t hundred_tables = 100 * per_table;
  std::cout << "  100 tables x 6 players ≈ " << (hundred_tables / 1024 / 1024) << " MB\n";
}
