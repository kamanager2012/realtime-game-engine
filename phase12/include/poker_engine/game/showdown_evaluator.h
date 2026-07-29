#pragma once
#include <map>
#include <string>
#include <vector>

#include "poker_engine/game/action.h"
#include "poker_engine/game/player_state.h"

namespace poker_engine::game {

struct EvalResult {
  int best_rank = 0;
  int strength = 0;
  uint8_t best_cards[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  bool operator<(const EvalResult& o) const { return best_rank < o.best_rank; }
};

struct ShowdownResult {
  int32_t player_id = 0;
  Chips amount_won = 0;
  std::string hand_description;
  int hand_rank = 0;
};

class ShowdownEvaluator {
 public:
  using CardId = uint8_t;

  static EvalResult Evaluate(const std::vector<CardId>& hole, const std::vector<CardId>& community);

  static std::vector<ShowdownResult> EvaluatePot(
      const std::vector<int32_t>& eligible_player_ids,
      const std::map<int32_t, std::vector<CardId>>& hole_cards_map,
      const std::vector<CardId>& community, Chips pot_amount);

  static std::string HandRankName(int rank);

 private:
  static int ScoreHand(const std::vector<CardId>& hand7, std::vector<CardId>& best_five_out);
};

}  // namespace poker_engine::game
