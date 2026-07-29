#include "poker_engine/tournament/tournament.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "poker_engine/economy/elo_rating.h"
#include "poker_engine/tournament/tournament_server.h"

using namespace poker_engine::tournament;
using namespace poker_engine::economy;

// ==================== Create and Start ====================

TEST(TournamentTest, CreateAndStart) {
  TournamentConfig config;
  config.name = "Test Freezeout";
  config.type = TournamentType::Freezeout;
  config.max_players = 6;
  config.players_per_table = 6;
  config.starting_stack = 1500;
  config.blind_schedule = TournamentConfig::GenerateTurboBlinds();

  TournamentManager tm(config);

  EXPECT_EQ(tm.Status(), TournamentStatus::Registration);
  EXPECT_EQ(tm.ActivePlayerCount(), 0);

  for (int i = 1; i <= 6; ++i) {
    EXPECT_TRUE(tm.RegisterPlayer(i, "P" + std::to_string(i)));
  }

  EXPECT_FALSE(tm.RegisterPlayer(7, "Overflow"));

  EXPECT_TRUE(tm.Start());
  EXPECT_EQ(tm.Status(), TournamentStatus::Running);
  EXPECT_EQ(tm.ActivePlayerCount(), 6);
  EXPECT_EQ(tm.TableCount(), 1);
}

// ==================== Registration Validation ====================

TEST(TournamentTest, RegistrationValidation) {
  TournamentConfig config;
  config.max_players = 3;
  config.blind_schedule = TournamentConfig::GenerateTurboBlinds();

  TournamentManager tm(config);

  EXPECT_TRUE(tm.RegisterPlayer(1, "Alice"));
  EXPECT_FALSE(tm.RegisterPlayer(1, "Alice Again"));
  EXPECT_TRUE(tm.RegisterPlayer(2, "Bob"));
  EXPECT_TRUE(tm.RegisterPlayer(3, "Carol"));
  EXPECT_FALSE(tm.RegisterPlayer(4, "Dave"));
}

// ==================== Blind Levels Increase ====================

TEST(TournamentTest, BlindLevelsIncrease) {
  TournamentConfig config;
  config.blind_schedule = TournamentConfig::GenerateTurboBlinds(10.0, 5);

  TournamentManager tm(config);

  double prev_bb = 0.0;
  for (int i = 0; i < 4; ++i) {
    double bb = tm.CurrentBigBlind();
    if (i > 0) {
      EXPECT_GE(bb, prev_bb) << "Blinds should be monotonically non-decreasing at level " << i;
    }
    prev_bb = bb;
    tm.AdvanceBlindLevel();
  }
}

// ==================== Elimination and Prizes ====================

TEST(TournamentTest, EliminationAndPrizes) {
  TournamentConfig config;
  config.max_players = 3;
  config.players_per_table = 3;
  config.starting_stack = 1000;
  config.payout_percentages = {60, 40};
  config.blind_schedule = TournamentConfig::GenerateTurboBlinds();

  TournamentManager tm(config);

  tm.RegisterPlayer(1, "Alice");
  tm.RegisterPlayer(2, "Bob");
  tm.RegisterPlayer(3, "Carol");

  EXPECT_TRUE(tm.Start());

  std::vector<std::pair<int, double>> results1 = {{1, 500.0}, {2, 500.0}, {3, -1000.0}};
  tm.ProcessHandComplete(0, results1);

  EXPECT_EQ(tm.ActivePlayerCount(), 2);
  EXPECT_EQ(tm.RemainingPlayerCount(), 2);

  tm.ProcessHandComplete(0, {{1, -1000.0}, {2, 1500.0}});

  EXPECT_LE(tm.ActivePlayerCount(), 1);

  if (tm.Status() == TournamentStatus::Completed) {
    double pool = tm.PrizePool();
    EXPECT_GT(pool, 0.0);
    EXPECT_NEAR(tm.GetPayout(0), pool * 0.6, 0.01);
    EXPECT_NEAR(tm.GetPayout(1), pool * 0.4, 0.01);
  }
}

// ==================== Leaderboard Order ====================

TEST(TournamentTest, LeaderboardOrder) {
  TournamentConfig config;
  config.max_players = 4;
  config.players_per_table = 4;
  config.starting_stack = 1000;
  config.payout_percentages = {50, 30, 20};
  config.blind_schedule = TournamentConfig::GenerateTurboBlinds();

  TournamentManager tm(config);

  tm.RegisterPlayer(1, "Alice");
  tm.RegisterPlayer(2, "Bob");
  tm.RegisterPlayer(3, "Carol");
  tm.RegisterPlayer(4, "Dave");

  tm.Start();

  tm.ProcessHandComplete(0, {{1, 500.0}, {2, 500.0}, {3, 500.0}, {4, -1500.0}});
  tm.ProcessHandComplete(0, {{1, 1000.0}, {2, -1500.0}, {3, 500.0}});

  auto result = tm.GetResult();
  auto& standings = result.final_standings;

  if (standings.size() >= 2) {
    bool found_active = false;
    bool found_elim = false;
    for (auto& p : standings) {
      if (!p.eliminated) found_active = true;
      if (p.eliminated) found_elim = true;
    }
    EXPECT_TRUE(found_active);
    if (found_elim) {
      for (size_t i = 0; i + 1 < standings.size(); ++i) {
        if (standings[i].eliminated && standings[i + 1].eliminated) {
          EXPECT_LE(standings[i].finish_position, standings[i + 1].finish_position);
        }
      }
    }
  }
}

// ==================== Bubble Detection ====================

TEST(TournamentTest, BubbleDetection) {
  TournamentConfig config;
  config.max_players = 4;
  config.players_per_table = 4;
  config.starting_stack = 1000;
  config.payout_percentages = {50, 30};
  config.blind_schedule = TournamentConfig::GenerateTurboBlinds();

  TournamentManager tm(config);

  tm.RegisterPlayer(1, "Alice");
  tm.RegisterPlayer(2, "Bob");
  tm.RegisterPlayer(3, "Carol");
  tm.RegisterPlayer(4, "Dave");

  tm.Start();

  int paid = tm.GetPaidPositions();
  EXPECT_EQ(paid, 2);

  EXPECT_FALSE(tm.IsBubble());

  tm.ProcessHandComplete(0, {{1, 500.0}, {2, 500.0}, {3, 500.0}, {4, -1500.0}});

  int remaining = tm.RemainingPlayerCount();
  if (remaining == paid + 1) {
    EXPECT_TRUE(tm.IsBubble());
  }
}

// ==================== Final Table Detection ====================

TEST(TournamentTest, FinalTableDetection) {
  TournamentConfig config;
  config.max_players = 9;
  config.players_per_table = 6;
  config.starting_stack = 1000;
  config.payout_percentages = {50, 30, 20};
  config.blind_schedule = TournamentConfig::GenerateTurboBlinds();

  TournamentManager tm(config);

  for (int i = 1; i <= 9; ++i) {
    tm.RegisterPlayer(i, "P" + std::to_string(i));
  }

  tm.Start();

  while (tm.ActivePlayerCount() > 6 && tm.Status() == TournamentStatus::Running) {
    std::vector<std::pair<int, double>> results;
    for (auto& [pid, p] : std::unordered_map<int, TournamentPlayer>(
             tm.GetResult().final_standings.begin(), tm.GetResult().final_standings.end())) {
      if (p.active && !p.eliminated) {
        results.push_back({pid, 0});
      }
    }
    if (results.empty()) break;

    int elim_id = results[0].first;
    results.push_back({elim_id, -1000.0});
    tm.ProcessHandComplete(0, results);
  }

  if (tm.ActivePlayerCount() <= 6 && tm.ActivePlayerCount() > 1) {
    EXPECT_TRUE(tm.IsFinalTable());
  }
}

// ==================== Builder Pattern ====================

TEST(TournamentTest, BuilderPattern) {
  TournamentConfig config = TournamentBuilder()
                                .WithName("Sunday Million")
                                .WithType(TournamentType::Rebuy)
                                .WithBuyIn(215.0, 15.0)
                                .WithStartingStack(10000)
                                .WithMaxPlayers(500)
                                .WithPlayersPerTable(9)
                                .WithGuarantee(1000000.0)
                                .WithRebuys(2, 200.0, 10000, 8)
                                .WithPayouts({18, 12, 8, 6, 5, 4, 3, 2.5, 2, 1.5})
                                .Build();

  EXPECT_EQ(config.name, "Sunday Million");
  EXPECT_EQ(config.type, TournamentType::Rebuy);
  EXPECT_DOUBLE_EQ(config.buy_in, 215.0);
  EXPECT_DOUBLE_EQ(config.entry_fee, 15.0);
  EXPECT_EQ(config.starting_stack, 10000);
  EXPECT_EQ(config.max_players, 500);
  EXPECT_EQ(config.players_per_table, 9);
  EXPECT_DOUBLE_EQ(config.guaranteed_prize_pool, 1000000.0);
  EXPECT_TRUE(config.has_rebuys);
  EXPECT_EQ(config.max_rebuys, 2);
  EXPECT_EQ(config.payout_percentages.size(), 10u);
}

// ==================== Turbo Blind Schedule ====================

TEST(TournamentTest, TurboBlindSchedule) {
  auto schedule = TournamentConfig::GenerateTurboBlinds(10.0, 5);

  ASSERT_GE(schedule.size(), 5u);

  EXPECT_EQ(schedule[0].duration_minutes, 5);

  for (size_t i = 1; i < schedule.size(); ++i) {
    EXPECT_GE(schedule[i].big_blind, schedule[i - 1].big_blind)
        << "Blinds should increase at level " << i;
  }
}

// ==================== Deep Stack Blind Schedule ====================

TEST(TournamentTest, DeepStackBlinds) {
  auto schedule = TournamentConfig::GenerateDeepStackBlinds(5.0, 5);

  ASSERT_GE(schedule.size(), 5u);

  EXPECT_EQ(schedule[0].duration_minutes, 30);

  for (size_t i = 1; i < schedule.size(); ++i) {
    EXPECT_GE(schedule[i].big_blind, schedule[i - 1].big_blind)
        << "Blinds should increase at level " << i;
  }
}

// ==================== Elo Tournament Processing ====================

TEST(TournamentTest, EloTournamentProcessing) {
  EloConfig config;
  config.k_factor = 32.0;
  config.initial_rating = 1500.0;

  std::vector<int> player_ids = {1, 2, 3};
  std::vector<int> positions = {1, 2, 3};
  std::vector<double> ratings = {1500.0, 1500.0, 1500.0};

  auto result = EloRating::ProcessTournament(player_ids, positions, ratings, config);

  ASSERT_EQ(result.ratings_after.size(), 3u);

  EXPECT_GT(result.ratings_after[0], 1500.0) << "1st place should gain rating";
  EXPECT_LT(result.ratings_after[2], 1500.0) << "3rd place should lose rating";

  double total_rating =
      std::accumulate(result.ratings_after.begin(), result.ratings_after.end(), 0.0);

  EXPECT_NEAR(total_rating, 4500.0, 1.0) << "Total rating should be approximately conserved";

  std::cout << "\n  Elo tournament results:" << std::endl;
  for (int i = 0; i < 3; ++i) {
    std::cout << "    Player " << player_ids[i] << ": " << ratings[i] << " -> "
              << result.ratings_after[i] << " (pos=" << positions[i] << ")" << std::endl;
  }
}

// ==================== Tournament Server Basic ====================

TEST(TournamentTest, TournamentServerCreateAndJoin) {
  TournamentServer server(9090);

  TournamentConfig config = TournamentBuilder()
                                .WithName("Test Tourney")
                                .WithMaxPlayers(6)
                                .WithPlayersPerTable(6)
                                .WithStartingStack(1500)
                                .WithPayouts({60, 40})
                                .Build();

  int tid = server.CreateTournament(config);
  EXPECT_GT(tid, 0);

  EXPECT_TRUE(server.JoinTournament(tid, 1, "Alice"));
  EXPECT_TRUE(server.JoinTournament(tid, 2, "Bob"));
  EXPECT_FALSE(server.JoinTournament(tid, 1, "Alice Again"));

  auto entry_cost = server.GetTournamentEntryCost(tid);
  ASSERT_TRUE(entry_cost.has_value());
  EXPECT_DOUBLE_EQ(*entry_cost, config.buy_in + config.entry_fee);

  auto state = server.GetTournamentStateJSON(tid);
  EXPECT_NE(state.find("Test Tourney"), std::string::npos);

  auto leaderboard = server.GetLeaderboardJSON(tid);
  EXPECT_FALSE(leaderboard.empty());
}
