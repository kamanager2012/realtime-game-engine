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

  // Collect EVERY player with chips invested this hand — including folded
  // players. Folded players' chips stay in the pot (dead money) but they are
  // never eligible to win. Excluding them here would destroy their chips,
  // violating conservation: Σ payouts must equal Σ total_invested.
  struct Contrib {
    int32_t id;
    Chips invested;
    bool folded;
  };
  std::vector<Contrib> contribs;
  for (auto* p : players) {
    if (!p) continue;
    Chips inv = p->bet_info.total_invested;
    if (inv <= 0) continue;
    contribs.push_back({p->id, inv, p->IsFolded()});
  }
  if (contribs.empty()) return pots_;

  std::sort(contribs.begin(), contribs.end(),
            [](const Contrib& a, const Contrib& b) { return a.invested < b.invested; });

  // Layered peeling: each layer subtracts the previous threshold
  Chips prev_level = 0;
  Chips pending_dead = 0;  // dead money from layers with no eligible claimant
  for (size_t i = 0; i < contribs.size(); ++i) {
    Chips level = contribs[i].invested;
    if (level <= prev_level) continue;

    Chips layer_height = level - prev_level;

    // Eligible: NON-folded players who invested >= level (index i..end)
    std::vector<int32_t> eligible;
    for (size_t j = i; j < contribs.size(); ++j) {
      if (!contribs[j].folded) eligible.push_back(contribs[j].id);
    }

    // All contributors at this layer (folded included) feed the pot.
    int contributors = static_cast<int>(contribs.size()) - static_cast<int>(i);
    Chips pot_amount = layer_height * contributors;

    if (eligible.empty()) {
      // Everyone who reached this layer folded — dead money. Hold it and
      // merge into the next live pot so the chips go to a real winner.
      pending_dead += pot_amount;
    } else {
      Pot pot(eligible);
      pot.Add(pot_amount + pending_dead);
      pending_dead = 0;
      pots_.push_back(std::move(pot));
    }

    prev_level = level;
  }

  // Defensive: a trailing dead layer with no live pot below it cannot occur
  // (at least one non-folded player always exists at hand end), but never
  // let chips vanish — attach to the last pot.
  if (pending_dead > 0 && !pots_.empty()) {
    pots_.back().Add(pending_dead);
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
