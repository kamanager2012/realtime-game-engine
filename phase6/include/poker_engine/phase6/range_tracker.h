#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "poker_engine/evaluator/card.h"
#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase6 {

enum class ActionObserved : uint8_t {
  FOLD = 0,
  CHECK,
  CALL,
  RAISE,
  BET_SMALL,
  BET_MEDIUM,
  BET_LARGE,
  ALL_IN,
  POST_BB
};

static constexpr const char* ActionObservedName[] = {
    "FOLD", "CHECK", "CALL", "RAISE", "BET_SMALL", "BET_MEDIUM", "BET_LARGE", "ALL_IN", "POST_BB"};

struct TrackingObservation {
  int street;  // 0=preflop, 1=flop, etc.
  ActionObserved action;
  double amount;  // relative to pot
  double pot;
  std::vector<poker_engine::Card> community_cards;
};

struct RangeTrackerResult {
  poker_engine::range::Range narrowed_range;
  double confidence;  // 0~1, how well we narrowed
  int remaining_combos;
  std::string reasoning;
  std::vector<std::pair<std::string, double>> top_hands;  // hand_name, weight
  std::string ToString() const;
};

class RangeTracker {
 public:
  RangeTracker();

  // Initialize with a prior range (e.g., from RangeBuilder)
  void SetPriorRange(const poker_engine::range::Range& range);
  void SetPriorRange(const std::string& range_str);

  // Feed observations sequentially
  void ObserveAction(const TrackingObservation& obs);

  // Converged range
  RangeTrackerResult GetCurrentRange() const;

  // Reset for new hand
  void Reset();

  // Configuration
  void SetAggressionProfile(double tightness);  // 0=loose, 1=tight

 private:
  poker_engine::range::Range prior_range_;
  poker_engine::range::Range current_weights_;  // float weights per hand
  std::vector<TrackingObservation> observations_;
  double aggression_profile_ = 0.5;
  int street_ = 0;
  double pot_ = 0;

  // Update weights based on action
  void UpdateWeights(ActionObserved action, double amount_rel,
                     const std::vector<poker_engine::Card>& community_cards);

  // Equity-based likelihood estimation
  static double ActionLikelihood(ActionObserved action, double equity, double amount_rel,
                                 double tightness);
};

}  // namespace phase6
}  // namespace poker_engine
