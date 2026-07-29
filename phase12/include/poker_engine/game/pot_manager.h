#pragma once
#include <string>
#include <vector>

#include "poker_engine/game/player_state.h"

namespace poker_engine::game {

struct Pot {
  Chips amount = 0;
  std::vector<int32_t> eligible_players;

  Pot() = default;
  Pot(const std::vector<int32_t>& players) : eligible_players(players) {}
  void Add(Chips amt) { amount += amt; }
  void Reset() {
    amount = 0;
    eligible_players.clear();
  }
  std::string ToString() const;
};

class PotManager {
 public:
  void Reset();
  std::vector<Pot> BuildPots(const std::vector<PlayerState*>& players);
  Chips TotalPot() const;
  Chips MainPot() const;
  Chips SidePotTotal() const;
  const std::vector<Pot>& GetPots() const { return pots_; }

 private:
  std::vector<Pot> pots_;
  Chips total_pot_ = 0;
};

}  // namespace poker_engine::game
