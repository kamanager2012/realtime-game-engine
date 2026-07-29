#include "poker_engine/phase6/range_tracker.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"

namespace poker_engine {
namespace phase6 {
using namespace poker_engine::range;
using namespace poker_engine::equity;
using namespace poker_engine::evaluator;

RangeTracker::RangeTracker() {}

void RangeTracker::SetPriorRange(const Range& range) {
  prior_range_ = range;
  current_weights_ = range;
  current_weights_.Normalize();
}

void RangeTracker::SetPriorRange(const std::string& range_str) {
  SetPriorRange(Range::FromString(range_str));
}

void RangeTracker::ObserveAction(const TrackingObservation& obs) {
  observations_.push_back(obs);
  street_ = obs.street + 1;
  pot_ = obs.pot;

  UpdateWeights(obs.action, obs.amount, obs.community_cards);
}

void RangeTracker::UpdateWeights(ActionObserved action, double amount_rel,
                                 const std::vector<Card>& community_cards) {
  Range opponent_estimate = current_weights_;

  double new_weights[1326];
  double total_weight = 0;

  uint8_t board5[5] = {0};
  int bs = 0;
  for (size_t i = 0; i < community_cards.size() && i < 5; i++) {
    board5[i] = community_cards[i].Id();
    bs = static_cast<int>(i + 1);
  }

  for (int i = 0; i < 1326; i++) {
    float w = current_weights_.Get(i);
    if (w <= 0) {
      new_weights[i] = 0;
      continue;
    }

    // Build single-hand range for equity calc
    auto [c1, c2] = poker_engine::range::HandId::Decode(static_cast<uint16_t>(i));
    Range hero_single;
    hero_single.Set(i, 1.0f);

    double equity = 0.5;
    if (bs > 0) {
      std::mt19937 rng(42 + i);
      auto res = EquityCalculator::CalculateMonteCarlo(hero_single, opponent_estimate, board5, bs,
                                                       3000, rng);
      equity = res.equity[0];
    } else {
      // Preflop: use Chen formula as fast equity proxy
      auto [c1, c2] = poker_engine::range::HandId::Decode(static_cast<uint16_t>(i));
      Card hc1(c1), hc2(c2);
      double r1 = static_cast<double>(hc1.GetRank());
      double r2 = static_cast<double>(hc2.GetRank());
      double hi = std::max(r1, r2);
      double lo = std::min(r1, r2);
      double chen = std::max(hi - 2, 0.0) * 1.5 + std::max(lo - 2, 0.0) * 0.5;
      if (hc1.GetSuit() == hc2.GetSuit()) chen += 2.0;
      if (r1 == r2) chen += 5.0;  // pocket pair
      double gap = hi - lo;
      if (gap == 1)
        chen -= 1.0;
      else if (gap == 2)
        chen -= 2.0;
      else if (gap == 3)
        chen -= 3.0;
      else if (gap >= 4)
        chen -= 4.0;
      // Map Chen score (0~20) to equity (0.2~0.85)
      equity = 0.2 + std::clamp(chen / 20.0, 0.0, 1.0) * 0.65;
    }

    double likelihood = ActionLikelihood(action, equity, amount_rel, aggression_profile_);

    new_weights[i] = w * likelihood;
    total_weight += new_weights[i];
  }

  if (total_weight > 0) {
    for (int i = 0; i < 1326; i++) {
      current_weights_.Set(i, static_cast<float>(new_weights[i] / total_weight));
    }
  }
}

double RangeTracker::ActionLikelihood(ActionObserved action, double equity, double amount_rel,
                                      double tightness) {
  auto sigmoid = [](double x, double k = 5.0) -> double { return 1.0 / (1.0 + std::exp(-k * x)); };

  double p = 0.01;

  switch (action) {
    case ActionObserved::FOLD:
      p = 1.0 - sigmoid(equity - 0.3, 8.0 * (1.0 + tightness));
      break;

    case ActionObserved::CHECK:
      if (equity < 0.15)
        p = 0.5;
      else if (equity < 0.5)
        p = 0.8;
      else
        p = 0.3;
      break;

    case ActionObserved::CALL:
      p = sigmoid(equity - 0.35, 6.0) * (1.0 - sigmoid(equity - 0.6, 8.0));
      break;

    case ActionObserved::RAISE:
      p = sigmoid(equity - 0.4, 10.0 * (1.0 + tightness));
      break;

    case ActionObserved::BET_SMALL:
      p = sigmoid(equity - 0.2, 5.0) * (1.0 - sigmoid(equity - 0.6, 6.0));
      break;

    case ActionObserved::BET_MEDIUM:
      p = sigmoid(equity - 0.3, 7.0 * (1.0 + tightness));
      break;

    case ActionObserved::BET_LARGE:
      p = sigmoid(equity - 0.2, 6.0);
      break;

    case ActionObserved::ALL_IN:
      if (tightness > 0.6) {
        p = sigmoid(equity - 0.5, 15.0);
      } else {
        p = sigmoid(equity - 0.3, 8.0);
      }
      break;

    case ActionObserved::POST_BB:
      p = 1.0;
      break;
  }

  return std::max(0.001, std::min(1.0, p));
}

RangeTrackerResult RangeTracker::GetCurrentRange() const {
  RangeTrackerResult result;
  result.narrowed_range = current_weights_;
  result.narrowed_range.Normalize();

  int combos = result.narrowed_range.NonZeroCount();
  result.remaining_combos = combos;

  double prior_nonzero = prior_range_.NonZeroCount();
  // Use entropy-based confidence instead of just combo count
  // Entropy of uniform over N = ln(N), after narrowing it's lower
  double prior_entropy = (prior_nonzero > 1) ? std::log(static_cast<double>(prior_nonzero)) : 0;
  double current_entropy = 0;
  for (int i = 0; i < 1326; i++) {
    float w = current_weights_.Get(i);
    if (w > 1e-8f) current_entropy -= w * std::log(static_cast<double>(w));
  }
  result.confidence =
      (prior_entropy > 0) ? std::clamp(1.0 - current_entropy / prior_entropy, 0.0, 1.0) : 0.0;

  std::ostringstream oss;
  oss << "After " << observations_.size() << " actions, narrowed from " << int(prior_nonzero)
      << " to " << combos << " combos (" << int(result.confidence * 100) << "% narrowed)";
  result.reasoning = oss.str();

  // Top 10 hands
  std::vector<std::pair<std::string, double>> hands;
  for (int i = 0; i < 1326; i++) {
    float w = current_weights_.Get(i);
    if (w > 0.001f) {
      auto [c1, c2] = poker_engine::range::HandId::Decode(static_cast<uint16_t>(i));
      std::string hand = Card(c1).ToString() + Card(c2).ToString();
      hands.push_back({hand, static_cast<double>(w)});
    }
  }
  std::sort(hands.begin(), hands.end(), [](auto& a, auto& b) { return a.second > b.second; });

  for (int i = 0; i < std::min(10, static_cast<int>(hands.size())); i++) {
    result.top_hands.push_back(hands[i]);
  }

  return result;
}

void RangeTracker::Reset() {
  prior_range_ = Range();
  current_weights_ = Range();
  observations_.clear();
  street_ = 0;
  pot_ = 0;
  aggression_profile_ = 0.5;
}

void RangeTracker::SetAggressionProfile(double tightness) {
  aggression_profile_ = std::clamp(tightness, 0.0, 1.0);
}

// ==================== RangeTrackerResult display ====================

std::string RangeTrackerResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "=== Range Tracker Result ===\n";
  oss << reasoning << "\n";
  oss << "Confidence: " << confidence * 100 << "%\n\n";

  oss << "Top Hands:\n";
  for (const auto& [hand, weight] : top_hands) {
    oss << "  " << hand << ": " << int(weight * 100 + 0.5) << "%\n";
  }

  int non_zero = narrowed_range.NonZeroCount();
  oss << "\nTotal combos: " << non_zero << " / 1326\n";

  return oss.str();
}

}  // namespace phase6
}  // namespace poker_engine
