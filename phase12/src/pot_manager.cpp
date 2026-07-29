#include "poker_engine/game/pot_manager.h"

#include <algorithm>
#include <numeric>
#include <sstream>

namespace poker_engine::game {

void PotManager::Reset() {
  pots_.clear();
  total_pot_ = 0;
}

Chips PotManager::TotalPot() const { return total_pot_; }

Chips PotManager::MainPot() const {
  if (pots_.empty()) return 0;
  return pots_.front().amount;
}

Chips PotManager::SidePotTotal() const {
  if (pots_.size() <= 1) return 0;
  Chips total = 0;
  for (size_t i = 1; i < pots_.size(); ++i) {
    total += pots_[i].amount;
  }
  return total;
}

std::vector<Pot> PotManager::BuildPots(const std::vector<PlayerState*>& players) {
  pots_.clear();
  total_pot_ = 0;

  // Collect non-folded players with their total investment, sorted ascending
  struct Contrib {
    int32_t id;
    Chips invested;
    bool all_in;
  };
  std::vector<Contrib> contribs;
  for (auto* p : players) {
    if (!p || p->IsFolded()) continue;
    Chips inv = p->bet_info.total_invested;
    if (inv <= 0) continue;
    contribs.push_back({p->id, inv, p->IsAllIn()});
  }
  if (contribs.empty()) return pots_;

  std::sort(contribs.begin(), contribs.end(),
            [](const Contrib& a, const Contrib& b) { return a.invested < b.invested; });

  // Layered peeling: each layer subtracts the previous threshold
  Chips prev_level = 0;
  for (size_t i = 0; i < contribs.size(); ++i) {
    Chips level = contribs[i].invested;
    if (level <= prev_level) continue;

    Chips layer_height = level - prev_level;

    // Eligible: all players who invested >= level (index i..end)
    std::vector<int32_t> eligible;
    for (size_t j = i; j < contribs.size(); ++j) {
      eligible.push_back(contribs[j].id);
    }

    // Count how many players contribute at this layer
    int contributors = static_cast<int>(contribs.size()) - static_cast<int>(i);
    Chips pot_amount = layer_height * contributors;

    Pot pot(eligible);
    pot.Add(pot_amount);
    pots_.push_back(std::move(pot));

    prev_level = level;
  }

  // Calculate total
  for (const auto& pot : pots_) {
    total_pot_ += pot.amount;
  }

  return pots_;
}

std::string Pot::ToString() const {
  std::ostringstream oss;
  oss << "Pot{amount=" << amount << " eligible=[";
  for (size_t i = 0; i < eligible_players.size(); ++i) {
    if (i > 0) oss << ",";
    oss << eligible_players[i];
  }
  oss << "]}";
  return oss.str();
}

}  // namespace poker_engine::game
