#include "poker_engine/phase3/batch_simulator.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase3 {

using namespace poker_engine;
using namespace poker_engine::range;
using poker_engine::equity::EquityCalculator;

BatchSimulator::BatchSimulator(const BatchConfig& config) : config_(config) {}

void BatchSimulator::AddPlayer(const std::string& name, const std::string& range_str) {
  AddPlayer(name, Range::FromString(range_str));
}

void BatchSimulator::AddPlayer(const std::string& name, const Range& range) {
  players_.push_back({name, range});
  stats_[name] = PlayerStats{name};
}

void BatchSimulator::DealCards(std::vector<uint8_t>& hero_cards_out) {
  hero_cards_out.clear();

  std::vector<uint8_t> deck;
  for (int i = 0; i < 52; i++) deck.push_back(i);

  for (size_t p = 0; p < players_.size(); p++) {
    int idx = rng_() % deck.size();
    hero_cards_out.push_back(deck[idx]);
    deck.erase(deck.begin() + idx);

    idx = rng_() % deck.size();
    hero_cards_out.push_back(deck[idx]);
    deck.erase(deck.begin() + idx);
  }
}

std::vector<Card> BatchSimulator::DealBoard(int num_cards, const std::vector<uint8_t>& exclude) {
  std::vector<Card> cards;
  std::vector<uint8_t> deck;
  for (int i = 0; i < 52; i++) {
    bool excluded = false;
    for (auto e : exclude)
      if (i == e) {
        excluded = true;
        break;
      }
    if (!excluded) deck.push_back(i);
  }

  for (int i = 0; i < num_cards && !deck.empty(); i++) {
    int idx = rng_() % deck.size();
    cards.push_back(Card(deck[idx]));
    deck.erase(deck.begin() + idx);
  }
  return cards;
}

std::string BatchSimulator::CardsToString(const std::vector<Card>& cards) const {
  std::string s;
  for (const auto& c : cards) s += c.ToString() + " ";
  return s;
}

std::vector<SimulationResult> BatchSimulator::Run(int num_hands) {
  std::vector<SimulationResult> results;

  for (int h = 0; h < num_hands; h++) {
    SimulationResult sr;
    sr.hand_id = ++hand_counter_;

    std::vector<uint8_t> all_hole_cards;
    DealCards(all_hole_cards);

    auto board = DealBoard(5, all_hole_cards);
    sr.board_str = CardsToString(board);

    uint8_t board5[5] = {0};
    for (int i = 0; i < 5 && i < (int)board.size(); i++) board5[i] = board[i].Id();

    for (size_t p = 0; p < players_.size(); p++) {
      Card c1(all_hole_cards[p * 2]);
      Card c2(all_hole_cards[p * 2 + 1]);
      sr.hero_cards_list.push_back(c1.ToString() + " " + c2.ToString());
      sr.player_names.push_back(players_[p].first);
      sr.equities.push_back(0);
    }

    if (players_.size() == 2) {
      uint8_t c1_1 = all_hole_cards[0], c1_2 = all_hole_cards[1];
      uint8_t c2_1 = all_hole_cards[2], c2_2 = all_hole_cards[3];

      Range hero_r;
      uint8_t lo1 = std::min(c1_1, c1_2), hi1 = std::max(c1_1, c1_2);
      if (lo1 != hi1) hero_r.Set(HandId::Encode(lo1, hi1), 1.0f);

      Range villain_r;
      uint8_t lo2 = std::min(c2_1, c2_2), hi2 = std::max(c2_1, c2_2);
      if (lo2 != hi2) villain_r.Set(HandId::Encode(lo2, hi2), 1.0f);

      if (hero_r.NonZeroCount() > 0 && villain_r.NonZeroCount() > 0) {
        auto res = EquityCalculator::CalculateMonteCarlo(hero_r, villain_r, board5, 5,
                                                         config_.iterations, rng_);
        sr.equities.clear();
        sr.equities.push_back(res.equity[0]);
        sr.equities.push_back(res.equity[1]);
        sr.equities.push_back(res.tie[0] + res.tie[1]);
      } else {
        sr.equities.clear();
        sr.equities.push_back(0.5);
        sr.equities.push_back(0.5);
        sr.equities.push_back(0.0);
      }
    } else {
      // Multiway: simplified — just compute hero vs field
      Range hero_r;
      uint8_t lo1 = std::min(all_hole_cards[0], all_hole_cards[1]);
      uint8_t hi1 = std::max(all_hole_cards[0], all_hole_cards[1]);
      if (lo1 != hi1) hero_r.Set(HandId::Encode(lo1, hi1), 1.0f);

      double hero_eq = 0.5;
      if (hero_r.NonZeroCount() > 0) {
        Range opp_r = players_[1].second;
        if (opp_r.NonZeroCount() > 0) {
          auto res = EquityCalculator::CalculateMonteCarlo(hero_r, opp_r, board5, 5,
                                                           config_.iterations, rng_);
          hero_eq = res.equity[0];
        }
      }

      sr.equities.clear();
      sr.equities.push_back(hero_eq);
      double remaining = 1.0 - hero_eq;
      for (size_t i = 1; i < players_.size(); i++) {
        sr.equities.push_back(remaining / (players_.size() - 1));
      }
    }

    double total_stack = config_.initial_stack * players_.size();
    sr.pot_size = total_stack * 0.1;

    for (size_t p = 0; p < players_.size(); p++) {
      double won = sr.pot_size * sr.equities[p];
      sr.final_stacks.push_back(config_.initial_stack - config_.big_blind + won);
      sr.starting_stacks.push_back(config_.initial_stack);

      auto& stat = stats_[players_[p].first];
      stat.hands_played++;
      stat.total_won += won - config_.big_blind;
      stat.total_invested += config_.big_blind;
      stat.avg_equity += sr.equities[p];
    }

    double max_eq = 0;
    size_t winner_idx = 0;
    for (size_t i = 0; i < sr.equities.size() - 1; i++) {
      if (sr.equities[i] > max_eq) {
        max_eq = sr.equities[i];
        winner_idx = i;
      }
    }
    sr.winner = players_[winner_idx].first + " (" + std::to_string(int(max_eq * 100)) + "%)";

    results.push_back(sr);

    if (config_.verbose && (h + 1) % 100 == 0) {
      std::cout << "\rSimulated " << (h + 1) << "/" << num_hands << " hands" << std::flush;
    }
  }

  if (config_.verbose && num_hands > 0) std::cout << std::endl;

  if (callback_) {
    for (const auto& sr : results) callback_(sr);
  }

  return results;
}

std::map<std::string, typename BatchSimulator::PlayerStats> BatchSimulator::GetStats() const {
  return stats_;
}

std::string BatchSimulator::StatsReport() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "\n=== Batch Simulation Statistics ===\n\n";
  oss << std::setw(16) << std::left << "Player" << std::setw(10) << "Hands" << std::setw(12)
      << "Net Won" << std::setw(10) << "ROI" << std::setw(10) << "Avg Eq%" << "\n";
  oss << std::string(60, '-') << "\n";

  for (const auto& [name, stat] : stats_) {
    double avg_eq = stat.hands_played > 0 ? stat.avg_equity / stat.hands_played * 100 : 0;
    oss << std::setw(16) << std::left << name << std::setw(10) << stat.hands_played << std::setw(12)
        << stat.total_won << std::setw(10) << stat.roi() << std::setw(10) << int(avg_eq) << "%"
        << "\n";
  }
  return oss.str();
}

void BatchSimulator::SetCallback(std::function<void(const SimulationResult&)> cb) {
  callback_ = cb;
}

}  // namespace phase3
}  // namespace poker_engine
