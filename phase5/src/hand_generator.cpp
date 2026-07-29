#include "poker_engine/phase5/hand_generator.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"

namespace poker_engine {
namespace phase5 {

using namespace poker_engine::range;
using poker_engine::equity::EquityCalculator;

HandGenerator::HandGenerator(uint32_t seed) : rng_(seed) {}

void HandGenerator::ShuffleDeck() {
  deck_.clear();
  for (int i = 0; i < 52; i++) deck_.push_back(i);
  std::shuffle(deck_.begin(), deck_.end(), rng_);
}

std::pair<uint8_t, uint8_t> HandGenerator::DealTwoCards() {
  if (deck_.size() < 2) ShuffleDeck();
  uint8_t c1 = deck_.back();
  deck_.pop_back();
  uint8_t c2 = deck_.back();
  deck_.pop_back();
  return {c1, c2};
}

std::vector<uint8_t> HandGenerator::DealBoard(int num_cards, const std::vector<uint8_t>& exclude) {
  std::vector<uint8_t> available;
  for (int i = 0; i < 52; i++) {
    bool excluded = false;
    for (auto e : exclude)
      if (i == e) {
        excluded = true;
        break;
      }
    if (!excluded) available.push_back(i);
  }
  std::shuffle(available.begin(), available.end(), rng_);

  std::vector<uint8_t> board;
  for (int i = 0; i < num_cards && i < static_cast<int>(available.size()); i++) {
    board.push_back(available[i]);
  }
  return board;
}

std::pair<poker_engine::Card, poker_engine::Card> HandGenerator::GenerateHand(const Range& range) {
  auto hand_id = range.Sample(rng_);
  auto [c1_id, c2_id] = poker_engine::range::HandId::Decode(hand_id);
  return {poker_engine::Card(c1_id), poker_engine::Card(c2_id)};
}

GeneratedHand HandGenerator::GenerateFullHand(const HandGenConfig& config) {
  GeneratedHand gh;
  gh.player_names.resize(config.num_players);
  gh.hole_cards.resize(config.num_players);
  gh.stacks.resize(config.num_players, 100.0);
  gh.button_pos = rng_() % config.num_players;

  const char* default_names[] = {"SB", "BB", "UTG", "MP", "CO", "BTN"};
  for (int i = 0; i < config.num_players && i < 6; i++) {
    gh.player_names[i] = default_names[i];
  }

  // Deal hole cards
  ShuffleDeck();
  std::vector<uint8_t> all_hole;
  for (int p = 0; p < config.num_players; p++) {
    auto [c1, c2] = DealTwoCards();
    all_hole.push_back(c1);
    all_hole.push_back(c2);
    gh.hole_cards[p] = poker_engine::Card(c1).ToString() + " " + poker_engine::Card(c2).ToString();
  }

  // Deal board
  auto board_ids = DealBoard(5, all_hole);
  std::string board_str;
  for (auto id : board_ids) {
    board_str += poker_engine::Card(id).ToString() + " ";
  }
  gh.board = board_str;

  // Compute equities (simplified: 2-player heads up)
  if (config.num_players >= 2) {
    uint8_t board5[5] = {0};
    for (int i = 0; i < 5 && i < static_cast<int>(board_ids.size()); i++) board5[i] = board_ids[i];

    Range hero_r;
    uint8_t lo1 = std::min(all_hole[0], all_hole[1]);
    uint8_t hi1 = std::max(all_hole[0], all_hole[1]);
    if (lo1 != hi1) hero_r.Set(HandId::Encode(lo1, hi1), 1.0f);

    Range villain_r;
    uint8_t lo2 = std::min(all_hole[2], all_hole[3]);
    uint8_t hi2 = std::max(all_hole[2], all_hole[3]);
    if (lo2 != hi2) villain_r.Set(HandId::Encode(lo2, hi2), 1.0f);

    if (hero_r.NonZeroCount() > 0 && villain_r.NonZeroCount() > 0) {
      auto res = EquityCalculator::CalculateMonteCarlo(hero_r, villain_r, board5, 5, 5000, rng_);
      gh.equities.push_back(res.equity[0]);
      gh.equities.push_back(res.equity[1]);
    }
  }

  return gh;
}

std::vector<GeneratedHand> HandGenerator::GenerateBatch(int num_hands,
                                                        const HandGenConfig& config) {
  std::vector<GeneratedHand> hands;
  for (int i = 0; i < num_hands; i++) {
    hands.push_back(GenerateFullHand(config));
  }
  return hands;
}

HandDistribution HandGenerator::ComputeDistribution(int num_samples, int num_players) {
  HandDistribution dist;
  dist.total_generated = num_samples;

  const char* ranks = "23456789TJQKA";

  for (int i = 0; i < num_samples; i++) {
    ShuffleDeck();
    auto [c1_id, c2_id] = DealTwoCards();

    poker_engine::Card c1(c1_id), c2(c2_id);
    int r1 = static_cast<int>(c1.GetRank()), r2 = static_cast<int>(c2.GetRank());
    int s1 = static_cast<int>(c1.GetSuit()), s2 = static_cast<int>(c2.GetSuit());

    std::string hand_name;
    if (r1 == r2) {
      hand_name = std::string(1, ranks[r1]) + std::string(1, ranks[r2]);
    } else {
      int hi = std::max(r1, r2), lo = std::min(r1, r2);
      hand_name = std::string(1, ranks[hi]) + std::string(1, ranks[lo]);
      hand_name += (s1 == s2) ? "s" : "o";
    }

    dist.hand_counts[hand_name]++;
  }

  for (const auto& [name, count] : dist.hand_counts) {
    dist.hand_frequencies[name] = static_cast<double>(count) / num_samples;
  }

  return dist;
}

std::string HandDistribution::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(4);
  oss << "Total: " << total_generated << "\n\n";
  oss << "Hand          Count  Frequency\n";
  oss << std::string(35, '-') << "\n";

  std::vector<std::pair<std::string, int>> sorted(hand_counts.begin(), hand_counts.end());
  std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });

  for (const auto& [name, count] : sorted) {
    oss << std::setw(14) << std::left << name << std::setw(7) << count << std::setw(10)
        << hand_frequencies.at(name) << "\n";
  }
  return oss.str();
}

const char* HandGenerator::PositionName(int pos) {
  static const char* names[] = {"SB", "BB", "UTG", "MP", "CO", "BTN"};
  return (pos >= 0 && pos < 6) ? names[pos] : "?";
}

Range HandGenerator::GetPositionRange(int pos) {
  static const char* ranges[] = {
      "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q9o+,J9o+,T8o+,97o+"
      ",86o+,75o+,64o+,53s",
      "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q9o+,J9o+,T8o+,97o+"
      ",86o+,75o+,64o+,53s",
      "77+,A2s+,K9s+,Q9s+,J9s+,T9s,98s,87s,76s,65s,54s,AQo+,KQo,QJo",
      "66+,A2s+,K9s+,Q9s+,J9s+,T8s+,97s+,87s,76s,65s,54s,AJo+,KQo,QJo",
      "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q9o+,J9o+,T8o+,97o+"
      ",86o+,75o+,64o+,53s",
      "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+"
      ",82o+,72o+,62o+,52o+,42o+"};
  return (pos >= 0 && pos < 6) ? Range::FromString(ranges[pos]) : Range::FullCombinatorial();
}

}  // namespace phase5
}  // namespace poker_engine
