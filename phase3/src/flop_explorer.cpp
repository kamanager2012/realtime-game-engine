#include "poker_engine/phase3/flop_explorer.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"

namespace poker_engine {
namespace phase3 {

using namespace poker_engine;
using namespace poker_engine::range;
using poker_engine::equity::EquityCalculator;

FlopExplorer::FlopExplorer() {}

void FlopExplorer::SetHeroRange(const Range& r) { hero_range_ = r; }
void FlopExplorer::SetVillainRange(const Range& r) { villain_range_ = r; }

FlopAnalysis FlopExplorer::AnalyzeFlop(const std::string& flop_cards, int n_samples) {
  FlopAnalysis result;
  result.flop_str = flop_cards;

  Card c1 = Card::Parse(flop_cards.substr(0, 2));
  Card c2 = Card::Parse(flop_cards.substr(2, 2));
  Card c3 = Card::Parse(flop_cards.substr(4, 2));

  uint8_t board[5] = {c1.Id(), c2.Id(), c3.Id(), 0, 0};

  std::mt19937 rng(42);
  auto res =
      EquityCalculator::CalculateMonteCarlo(hero_range_, villain_range_, board, 3, n_samples, rng);

  result.hero_equity = res.equity[0];
  result.villain_equity = res.equity[1];
  result.tie_pct = res.tie[0] + res.tie[1];

  result.hero_wins = static_cast<int>(res.win[0]);
  result.villain_wins = static_cast<int>(res.win[1]);
  result.ties = static_cast<int>((res.tie[0] + res.tie[1]) * n_samples);

  return result;
}

std::string FlopAnalysis::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "Flop: " << flop_str << " | Hero: " << hero_equity * 100 << "%"
      << " | Villain: " << villain_equity * 100 << "%"
      << " | Tie: " << tie_pct * 100 << "%"
      << " (W" << hero_wins << "/L" << villain_wins << "/T" << ties << ")";
  return oss.str();
}

bool FlopExplorer::IsPairedFlop(const std::string& flop) {
  char r1 = flop[0], r2 = flop[2], r3 = flop[4];
  return r1 == r2 || r2 == r3 || r1 == r3;
}

bool FlopExplorer::IsMonotoneFlop(const std::string& flop) {
  char s1 = flop[1], s2 = flop[3], s3 = flop[5];
  return s1 == s2 && s2 == s3;
}

bool FlopExplorer::IsConnectedFlop(const std::string& flop) {
  auto rank_idx = [](char r) -> int {
    switch (r) {
      case '2':
        return 0;
      case '3':
        return 1;
      case '4':
        return 2;
      case '5':
        return 3;
      case '6':
        return 4;
      case '7':
        return 5;
      case '8':
        return 6;
      case '9':
        return 7;
      case 'T':
        return 8;
      case 'J':
        return 9;
      case 'Q':
        return 10;
      case 'K':
        return 11;
      case 'A':
        return 12;
      default:
        return -1;
    }
  };
  int r1 = rank_idx(flop[0]), r2 = rank_idx(flop[2]), r3 = rank_idx(flop[4]);
  std::array<int, 3> ranks = {r1, r2, r3};
  std::sort(ranks.begin(), ranks.end());
  return (ranks[1] - ranks[0] <= 1) && (ranks[2] - ranks[1] <= 1);
}

std::string FlopExplorer::CategoryName(const std::string& flop) {
  bool paired = IsPairedFlop(flop);
  bool monotone = IsMonotoneFlop(flop);
  bool connected = IsConnectedFlop(flop);

  if (paired && monotone) return "Paired+Monotone";
  if (paired) return "Paired";
  if (monotone && connected) return "Monotone+Connected";
  if (monotone) return "Monotone";
  if (connected) return "Connected";
  return "Dry/Rag";
}

void FlopExplorer::AnalyzeFlopCategories(int n_samples) {
  std::cout << "\n=== Flop Category Analysis ===\n\n";

  std::map<std::string, std::vector<double>> category_results;

  const char* ranks = "23456789TJQKA";
  const char* suits = "shdc";

  int count = 0;
  for (int i = 0; i < 13; i++) {
    for (int j = i; j < 13; j++) {
      for (int k = j; k < 13; k++) {
        char f[7];
        f[0] = ranks[i];
        f[1] = suits[0];
        f[2] = ranks[j];
        f[3] = suits[1];
        f[4] = ranks[k];
        f[5] = suits[2];
        f[6] = '\0';

        if (f[0] == f[2] && f[0] == f[4])
          continue;  // skip impossible 3-of-a-kind flops in real deck

        auto analysis = AnalyzeFlop(f, n_samples);
        std::string cat = CategoryName(f);

        category_results[cat].push_back(analysis.hero_equity);
        count++;

        if (count % 50 == 0) std::cout << "\rAnalyzed " << count << " flops..." << std::flush;
      }
    }
  }
  std::cout << "\rTotal flops analyzed: " << count << "\n\n";

  std::cout << std::fixed << std::setprecision(2);
  std::cout << std::setw(22) << std::left << "Category" << std::setw(10) << "Count" << std::setw(10)
            << "Avg Eq%" << std::setw(10) << "Min%" << std::setw(10) << "Max%" << "\n";
  std::cout << std::string(62, '-') << "\n";

  for (const auto& [cat, eqs] : category_results) {
    double avg = std::accumulate(eqs.begin(), eqs.end(), 0.0) / eqs.size();
    double mn = *std::min_element(eqs.begin(), eqs.end());
    double mx = *std::max_element(eqs.begin(), eqs.end());
    std::cout << std::setw(22) << std::left << cat << std::setw(10) << eqs.size() << std::setw(10)
              << avg * 100 << std::setw(10) << mn * 100 << std::setw(10) << mx * 100 << "\n";
  }
}

RunoutStats FlopExplorer::AnalyzeTurnRunouts(const std::string& flop_cards, int n_samples) {
  RunoutStats stats;

  Card c1 = Card::Parse(flop_cards.substr(0, 2));
  Card c2 = Card::Parse(flop_cards.substr(2, 2));
  Card c3 = Card::Parse(flop_cards.substr(4, 2));

  std::array<uint8_t, 3> flop_ids = {c1.Id(), c2.Id(), c3.Id()};
  std::array<bool, 52> used = {};
  for (auto id : flop_ids) used[id] = true;

  std::vector<uint8_t> remaining;
  for (int i = 0; i < 52; i++)
    if (!used[i]) remaining.push_back(i);

  std::mt19937 rng(42);
  double total_eq = 0;

  for (int turn_idx : remaining) {
    uint8_t board[5] = {flop_ids[0], flop_ids[1], flop_ids[2], (uint8_t)turn_idx, 0};
    auto res = EquityCalculator::CalculateMonteCarlo(hero_range_, villain_range_, board, 4,
                                                     n_samples, rng);
    stats.hero_wins_by_card[turn_idx] = static_cast<int>(res.win[0]);
    total_eq += res.equity[0];
  }

  stats.total_runouts = remaining.size();
  stats.avg_equity = total_eq / remaining.size();

  double variance = 0;
  for (int i = 0; i < stats.total_runouts; i++) {
    double eq = stats.hero_wins_by_card[i] / (double)n_samples;
    variance += (eq - stats.avg_equity) * (eq - stats.avg_equity);
  }
  stats.equity_std = std::sqrt(variance / stats.total_runouts);

  return stats;
}

}  // namespace phase3
}  // namespace poker_engine
