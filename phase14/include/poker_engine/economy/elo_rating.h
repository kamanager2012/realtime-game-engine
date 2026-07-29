#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace poker_engine::economy {

struct EloConfig {
  double initial_rating = 1500.0;
  double k_factor = 32.0;
  double k_factor_new = 40.0;
  double k_factor_established = 20.0;
  int established_games = 30;
  double rating_floor = 100.0;
  double rating_ceiling = 4000.0;
  double performance_ceiling = 800.0;
  bool use_fide_adjustment = false;
  double fide_threshold_2400 = 2400.0;
  double fide_k_reduction = 10.0;
};

class EloRating {
 public:
  static double ExpectedScore(double rating_a, double rating_b) {
    return 1.0 / (1.0 + std::pow(10.0, (rating_b - rating_a) / 400.0));
  }

  static double UpdateRatingWithK(double rating, double expected, double actual, double k_factor) {
    return rating + k_factor * (actual - expected);
  }

  static double UpdateRating(double rating, double expected, double actual,
                             const EloConfig& config = EloConfig()) {
    double k = KFactorForPlayer(rating, 0, config);
    return UpdateRatingWithK(rating, expected, actual, k);
  }

  struct DuelResult {
    double rating_a_before;
    double rating_b_before;
    double rating_a_after;
    double rating_b_after;
    double expected_a;
    double expected_b;
    double score_a;
    double score_b;
  };

  static DuelResult ProcessDuel(double rating_a, double rating_b, double score_a, double score_b,
                                const EloConfig& config = EloConfig()) {
    DuelResult result;
    result.rating_a_before = rating_a;
    result.rating_b_before = rating_b;
    result.score_a = score_a;
    result.score_b = score_b;
    result.expected_a = ExpectedScore(rating_a, rating_b);
    result.expected_b = 1.0 - result.expected_a;
    result.rating_a_after = UpdateRating(rating_a, result.expected_a, score_a, config);
    result.rating_b_after = UpdateRating(rating_b, result.expected_b, score_b, config);
    return result;
  }

  struct Placement {
    int player_id;
    int position;
  };

  struct TournamentResult {
    std::vector<Placement> placements;
    std::vector<double> ratings_before;
    std::vector<double> ratings_after;
    double quality_score = 0.0;
  };

  static TournamentResult ProcessTournament(const std::vector<int>& player_ids,
                                            const std::vector<int>& positions,
                                            const std::vector<double>& ratings,
                                            const EloConfig& config = EloConfig()) {
    TournamentResult result;
    int n = static_cast<int>(player_ids.size());
    result.ratings_before = ratings;
    result.ratings_after = ratings;
    for (int i = 0; i < n; ++i) {
      for (int j = i + 1; j < n; ++j) {
        double score_i = positions[i] < positions[j]   ? 1.0
                         : positions[i] > positions[j] ? 0.0
                                                       : 0.5;
        auto duel = ProcessDuel(result.ratings_after[i], result.ratings_after[j], score_i,
                                1.0 - score_i, config);
        result.ratings_after[i] = duel.rating_a_after;
        result.ratings_after[j] = duel.rating_b_after;
      }
      result.placements.push_back({player_ids[i], positions[i]});
    }
    return result;
  }

  static double RatingToWinProbability(double rating_diff) {
    return ExpectedScore(1500.0 + rating_diff, 1500.0);
  }

  static double RatingToClass(double rating) {
    if (rating < kBeginnerThreshold) return 0.0;
    if (rating < kNoviceThreshold) return 1.0;
    if (rating < kIntermediateThreshold) return 2.0;
    if (rating < kAdvancedThreshold) return 3.0;
    if (rating < kExpertThreshold) return 4.0;
    if (rating < kMasterThreshold) return 5.0;
    return 6.0;
  }

  static double ComputePerformanceRating(const std::vector<double>& opponent_ratings,
                                         const std::vector<double>& scores, int num_games) {
    if (num_games <= 0) return 1500.0;
    double total_opp = 0.0;
    double total_score = 0.0;
    for (int i = 0; i < num_games && i < static_cast<int>(opponent_ratings.size()); ++i) {
      total_opp += opponent_ratings[i];
      total_score += scores[i];
    }
    double avg_opp = total_opp / num_games;
    double win_rate = total_score / num_games;
    if (win_rate <= 0.0) return avg_opp - 800.0;
    if (win_rate >= 1.0) return avg_opp + 800.0;
    return avg_opp + 400.0 * std::log10(win_rate / (1.0 - win_rate));
  }

  static double KFactorForPlayer(double rating, int games_played,
                                 const EloConfig& config = EloConfig()) {
    if (games_played < config.established_games) return config.k_factor_new;
    if (config.use_fide_adjustment && rating >= config.fide_threshold_2400)
      return config.k_factor_established - config.fide_k_reduction;
    return config.k_factor;
  }

  struct RatingClass {
    static constexpr const char* Beginner = "Beginner";
    static constexpr const char* Novice = "Novice";
    static constexpr const char* Intermediate = "Intermediate";
    static constexpr const char* Advanced = "Advanced";
    static constexpr const char* Expert = "Expert";
    static constexpr const char* Master = "Master";
    static constexpr const char* Grandmaster = "Grandmaster";
  };

  static constexpr double kBeginnerThreshold = 800.0;
  static constexpr double kNoviceThreshold = 1000.0;
  static constexpr double kIntermediateThreshold = 1200.0;
  static constexpr double kAdvancedThreshold = 1500.0;
  static constexpr double kExpertThreshold = 1800.0;
  static constexpr double kMasterThreshold = 2100.0;
};

}  // namespace poker_engine::economy
