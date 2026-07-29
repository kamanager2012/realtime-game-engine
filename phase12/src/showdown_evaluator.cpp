#include "poker_engine/game/showdown_evaluator.h"

#include <algorithm>
#include <map>
#include <sstream>

#include "poker_engine/base/types.h"
#include "phevaluator/phevaluator.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/evaluator/evaluator.h"

namespace poker_engine::game {

namespace {
// Best 5-card hand from a set of >=5 raw card ids. Delegates to the
// single-pass Evaluator::Evaluate7 for the common 7-card case and the
// brute-force C(n,5) enumeration otherwise. Returns the EvalResult plus
// the 5 constituent card ids.
void BestFromCards(const std::vector<CardId>& all, EvalResult& out_result,
                  uint8_t out_best[5]) {
  int n = static_cast<int>(all.size());
  int best_val = -1;
  if (n == 7) {
    uint8_t ids7[7];
    for (int i = 0; i < 7; ++i) ids7[i] = static_cast<uint8_t>(all[i]);
    auto eval = poker_engine::evaluator::Evaluator::Evaluate7(ids7);
    best_val = static_cast<int>(eval.value());
    // Recover the 5 constituent cards by testing each C(7,5) subset.
    constexpr int kCombos21[21][5] = {
        {0, 1, 2, 3, 4}, {0, 1, 2, 3, 5}, {0, 1, 2, 3, 6}, {0, 1, 2, 4, 5}, {0, 1, 2, 4, 6},
        {0, 1, 2, 5, 6}, {0, 1, 3, 4, 5}, {0, 1, 3, 4, 6}, {0, 1, 3, 5, 6}, {0, 1, 4, 5, 6},
        {0, 2, 3, 4, 5}, {0, 2, 3, 4, 6}, {0, 2, 3, 5, 6}, {0, 2, 4, 5, 6}, {0, 3, 4, 5, 6},
        {1, 2, 3, 4, 5}, {1, 2, 3, 4, 6}, {1, 2, 3, 5, 6}, {1, 2, 4, 5, 6}, {1, 3, 4, 5, 6},
        {2, 3, 4, 5, 6},
    };
    for (int c = 0; c < 21; ++c) {
      uint8_t sub[5];
      for (int i = 0; i < 5; ++i) sub[i] = static_cast<uint8_t>(all[kCombos21[c][i]]);
      poker_engine::Card cards5[5];
      for (int i = 0; i < 5; ++i) cards5[i] = poker_engine::Card(sub[i]);
      auto e = poker_engine::evaluator::Evaluator::Evaluate5(cards5);
      if (static_cast<int>(e.value()) == best_val) {
        for (int i = 0; i < 5; ++i) out_best[i] = sub[i];
        break;
      }
    }
  } else {
    // General case: enumerate C(n,5) combinations.
    std::vector<int> indices(n, 0);
    std::fill(indices.begin(), indices.begin() + 5, 1);
    std::sort(indices.begin(), indices.end());
    do {
      uint8_t sub[5];
      int ci = 0;
      for (int i = 0; i < n && ci < 5; ++i)
        if (indices[i]) sub[ci++] = static_cast<uint8_t>(all[i]);
      poker_engine::Card cards5[5];
      for (int i = 0; i < 5; ++i) cards5[i] = poker_engine::Card(sub[i]);
      auto e = poker_engine::evaluator::Evaluator::Evaluate5(cards5);
      int val = static_cast<int>(e.value());
      if (val > best_val) {
        best_val = val;
        ci = 0;
        for (int i = 0; i < n && ci < 5; ++i)
          if (indices[i]) out_best[ci++] = static_cast<uint8_t>(all[i]);
      }
    } while (std::next_permutation(indices.begin(), indices.end()));
  }
  out_result.best_rank = best_val;
  out_result.strength = best_val;
}
}  // namespace

EvalResult ShowdownEvaluator::Evaluate(const std::vector<CardId>& hole,
                                       const std::vector<CardId>& community) {
  // Omaha: 4 hole cards + 5 community, must use exactly 2 from hole + 3 from community.
  // The Omaha evaluator needs large pre-generated lookup tables that are omitted
  // from the distribution. When PHEVAL_HAVE_PLO is compiled in, use
  // PokerHandEvaluator's Omaha evaluator; otherwise Omaha is reported as
  // unsupported in this build (the Hold'em path below remains fully functional).
  if (hole.size() == 4 && community.size() == 5) {
#ifdef PHEVAL_HAVE_PLO
    EvalResult result;
    std::fill(result.best_cards, result.best_cards + 5, 0xFF);
    int rank = evaluate_omaha_cards(
        community[0], community[1], community[2], community[3], community[4],
        hole[0], hole[1], hole[2], hole[3]);
    result.best_rank = rank;
    result.strength = rank;
    return result;
#else
    // Omaha tables not compiled in. Return a sentinel so callers can detect
    // that Omaha evaluation is unavailable in this build.
    EvalResult result;
    std::fill(result.best_cards, result.best_cards + 5, 0xFF);
    result.best_rank = -1;
    result.strength = -1;
    return result;
#endif
  }

  // NLHE: Combine hole + community into 7 cards
  std::vector<CardId> all7;
  all7.reserve(7);
  all7.insert(all7.end(), hole.begin(), hole.end());
  all7.insert(all7.end(), community.begin(), community.end());

  EvalResult result;
  result.best_rank = 0;
  result.strength = 0;
  std::fill(result.best_cards, result.best_cards + 5, 0xFF);

  if (all7.size() < 5) return result;

  if (all7.size() == 5) {
    poker_engine::Card cards5[5];
    for (int i = 0; i < 5; ++i) cards5[i] = poker_engine::Card(all7[i]);
    auto eval = poker_engine::evaluator::Evaluator::Evaluate5(cards5);
    result.best_rank = static_cast<int>(eval.value());
    result.strength = static_cast<int>(eval.value());
    for (int i = 0; i < 5; ++i) result.best_cards[i] = all7[i];
    return result;
  }

  uint8_t best_five[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  BestFromCards(all7, result, best_five);
  for (int i = 0; i < 5; ++i) result.best_cards[i] = best_five[i];
  return result;
}

std::vector<ShowdownResult> ShowdownEvaluator::EvaluatePot(
    const std::vector<int32_t>& eligible_player_ids,
    const std::map<int32_t, std::vector<CardId>>& hole_cards_map,
    const std::vector<CardId>& community, Chips pot_amount) {
  if (eligible_player_ids.empty() || pot_amount <= 0) {
    return {};
  }

  // Evaluate each eligible player's hand
  std::vector<std::pair<EvalResult, int32_t>> evals;
  for (auto pid : eligible_player_ids) {
    auto it = hole_cards_map.find(pid);
    if (it == hole_cards_map.end()) continue;
    auto res = Evaluate(it->second, community);
    evals.emplace_back(res, pid);
  }

  if (evals.empty()) return {};

  // Sort descending by best_rank (higher = better)
  std::sort(evals.begin(), evals.end(),
            [](const auto& a, const auto& b) { return a.first.best_rank > b.first.best_rank; });

  // Find all players tied for the best hand
  int best = evals.front().first.best_rank;
  std::vector<int32_t> winners;
  for (const auto& [ev, pid] : evals) {
    if (ev.best_rank == best)
      winners.push_back(pid);
    else
      break;
  }

  // Split pot among winners using exact integer division. The remainder
  // (pot_amount % winners.size()) is distributed one chip at a time to the
  // first N winners, guaranteeing chip conservation (Σ payouts == pot_amount).
  Chips base = pot_amount / static_cast<Chips>(winners.size());
  Chips remainder = pot_amount % static_cast<Chips>(winners.size());
  std::vector<ShowdownResult> results;
  for (size_t wi = 0; wi < winners.size(); ++wi) {
    auto pid = winners[wi];
    ShowdownResult sr;
    sr.player_id = pid;
    sr.amount_won = base + (wi < static_cast<size_t>(remainder) ? 1 : 0);
    // Find the eval result for this player
    for (const auto& [ev, eid] : evals) {
      if (eid == pid) {
        sr.hand_description = HandRankName(ev.best_rank);
        sr.hand_rank = ev.best_rank;
        break;
      }
    }
    results.push_back(sr);
  }

  return results;
}

std::string ShowdownEvaluator::HandRankName(int rank) {
  // Extract HandCategory from the rank value
  // value() = category << 20 | rank[0] << 16 | rank[1] << 12 | ...
  // Category is in the top nibble
  if (rank <= 0) return "Unknown";
  int cat_val = (rank >> 20) & 0xF;
  auto cat = static_cast<poker_engine::HandCategory>(cat_val);
  return poker_engine::CategoryName(cat);
}

int ShowdownEvaluator::ScoreHand(const std::vector<CardId>& hand7,
                                 std::vector<CardId>& best_five_out) {
  auto result = Evaluate(
      std::vector<CardId>(hand7.begin(), hand7.begin() + std::min<size_t>(hand7.size(), 7)),
      std::vector<CardId>());
  best_five_out.assign(result.best_cards, result.best_cards + 5);
  return result.best_rank;
}

}  // namespace poker_engine::game
