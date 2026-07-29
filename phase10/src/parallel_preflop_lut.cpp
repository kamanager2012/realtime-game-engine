#include "poker_engine/phase10/parallel_preflop_lut.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase10 {
using namespace poker_engine::range;
using namespace poker_engine::equity;
using namespace poker_engine::evaluator;

std::string PreflopHandType::ToString() const {
  const char* ranks = "23456789TJQKA";
  std::string s;
  s += ranks[std::clamp(rank1, 0, 12)];
  s += ranks[std::clamp(rank2, 0, 12)];
  if (rank1 == rank2) return s;  // pairs
  s += suited ? 's' : 'o';
  return s;
}

int16_t PreflopHandType::ToIndex(int r1, int r2, bool suited) {
  int hi = std::max(r1, r2), lo = std::min(r1, r2);
  if (hi == lo) return 12 - hi;  // pairs: 0-12
  int idx = 13;
  for (int r = 12; r >= 1; r--) {
    for (int c = r - 1; c >= 0; c--) {
      if (r == hi && c == lo) return idx + (suited ? 0 : 78);
      idx++;
    }
  }
  return 168;
}

PreflopHandType PreflopHandType::FromIndex(int16_t idx) {
  PreflopHandType ht;
  if (idx < 13) {
    ht.rank1 = 12 - idx;
    ht.rank2 = ht.rank1;
    return ht;
  }
  idx -= 13;
  bool suited = idx < 78;
  if (!suited) idx -= 78;
  int count = 0;
  for (int r = 12; r >= 1; r--) {
    for (int c = r - 1; c >= 0; c--) {
      if (count == idx) {
        ht.rank1 = r;
        ht.rank2 = c;
        ht.suited = suited;
        return ht;
      }
      count++;
    }
  }
  ht.rank1 = 12;
  ht.rank2 = 11;
  ht.suited = true;
  return ht;
}

PreflopHandType PreflopHandType::FromHandName(const std::string& name) {
  PreflopHandType ht;
  if (name.size() < 2) {
    ht.rank1 = 12;
    ht.rank2 = 12;
    return ht;
  }
  const char* ranks = "23456789TJQKA";
  auto find_rank = [ranks](char c) -> int {
    for (int i = 0; i < 13; i++)
      if (ranks[i] == c) return i;
    return 12;
  };
  ht.rank1 = find_rank(name[0]);
  ht.rank2 = find_rank(name[1]);
  ht.suited = (name.size() >= 3 && name[2] == 's');
  return ht;
}

void PreflopLUT::SaveToFile(const std::string& filepath) const {
  std::ofstream out(filepath, std::ios::binary);
  if (!out) return;
  out.write(reinterpret_cast<const char*>(this), sizeof(PreflopLUT));
}

bool PreflopLUT::LoadFromFile(const std::string& filepath) {
  std::ifstream in(filepath, std::ios::binary);
  if (!in) return false;
  in.read(reinterpret_cast<char*>(this), sizeof(PreflopLUT));
  return in.good();
}

std::string PreflopLUT::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << "=== Pre-flop LUT ===\n";
  oss << "Hand | vs Random | vs 1BB | EV/1BB\n";
  oss << std::string(40, '-') << "\n";
  std::vector<std::pair<int, float>> ranked;
  for (int i = 0; i < NUM_TYPES; i++) ranked.push_back({i, ev_vs_1bb[i]});
  std::sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.second > b.second; });
  for (int i = 0; i < std::min(25, NUM_TYPES); i++) {
    auto ht = PreflopHandType::FromIndex(ranked[i].first);
    oss << "  " << std::setw(6) << ht.ToString() << " | " << std::setw(5)
        << equity_vs_random[ranked[i].first] * 100 << "%"
        << " | " << std::setw(5) << equity_vs_1bbrange[ranked[i].first] * 100 << "%"
        << " | " << std::setw(5) << int(ev_vs_1bb[ranked[i].first]) << " BB\n";
  }
  return oss.str();
}

float PreflopLUTCalculator::QueryEquity(const PreflopLUT& lut, const std::string& hand_a,
                                        const std::string& hand_b) {
  auto ht_a = PreflopHandType::FromHandName(hand_a);
  auto ht_b = PreflopHandType::FromHandName(hand_b);
  int idx_a = PreflopHandType::ToIndex(ht_a.rank1, ht_a.rank2, ht_a.suited);
  int idx_b = PreflopHandType::ToIndex(ht_b.rank1, ht_b.rank2, ht_b.suited);
  return lut.GetEquity(std::clamp(idx_a, 0, 168), std::clamp(idx_b, 0, 168));
}

PreflopLUTCalculator::PreflopLUTCalculator(int num_threads)
    : num_threads_(num_threads > 0 ? num_threads : ParallelFor::GetNumThreads()) {}

PreflopLUT PreflopLUTCalculator::Calculate(int mc_samples) {
  PreflopLUT lut = {};
  auto all_types = GenerateAllHandTypes();

  std::cout << "=== Pre-flop LUT Calculation ===\n";
  std::cout << "169 types | " << mc_samples << " MC samples | " << num_threads_ << " threads\n";

  Stopwatch sw;

  // Phase 1: 169x169 matrix (parallel)
  int total_pairs = all_types.size() * (all_types.size() + 1) / 2;
  std::atomic<int> completed{0};

  ParallelFor::Init(num_threads_);
  ParallelFor::Range(0, static_cast<int>(all_types.size()), [&](int i) {
    Range hero_range = Range::FromString(all_types[i].ToString());
    for (int j = 0; j <= i; j++) {
      Range villain_range = Range::FromString(all_types[j].ToString());
      std::mt19937 rng(42 + i * 169 + j);
      uint8_t b5[5] = {0};
      auto res =
          EquityCalculator::CalculateMonteCarlo(hero_range, villain_range, b5, 0, mc_samples, rng);
      float eq = static_cast<float>(res.equity[0]);
      lut.SetEquity(i, j, eq);
      lut.SetEquity(j, i, 1.0f - eq);
      if (i == j) lut.SetEquity(i, j, 0.5f);
      int done = completed.fetch_add(1) + 1;
      if (done % 1000 == 0)
        std::cout << "\r  Matrix: " << int(100.0 * done / total_pairs) << "%" << std::flush;
    }
  });
  std::cout << "\r  Matrix: 100%\n";

  // Phase 2: vs range stats (parallel)
  Range random_range = Range::FullCombinatorial();
  Range bb_range = Range::FromString("22+,A2s+,K2s+,Q2s+,J2s+,T2s+,A2o+,K2o+");
  uint8_t b5[5] = {0};

  ParallelFor::Range(0, static_cast<int>(all_types.size()), [&](int i) {
    Range hero_range = Range::FromString(all_types[i].ToString());
    std::mt19937 rng1(100000 + i);
    auto res1 = EquityCalculator::CalculateMonteCarlo(hero_range, random_range, b5, 0, 1000, rng1);
    lut.equity_vs_random[i] = static_cast<float>(res1.equity[0]);
    std::mt19937 rng2(200000 + i);
    auto res2 = EquityCalculator::CalculateMonteCarlo(hero_range, bb_range, b5, 0, 1000, rng2);
    lut.equity_vs_1bbrange[i] = static_cast<float>(res2.equity[0]);
    double pot = 2.5;
    lut.ev_vs_1bb[i] = static_cast<float>(res2.equity[0] * pot - (1.0 - res2.equity[0]) * 1.0);
  });

  std::cout << "  Done in " << sw.ElapsedSeconds() << "s\n\n";
  std::cout << lut.ToString();
  return lut;
}

std::vector<PreflopHandType> PreflopLUTCalculator::GenerateAllHandTypes() {
  std::vector<PreflopHandType> types;
  for (int r = 12; r >= 0; r--) types.push_back({r, r, false});  // 13 pairs
  for (int r1 = 12; r1 >= 1; r1--)                               // 78 suited
    for (int r2 = r1 - 1; r2 >= 0; r2--) types.push_back({r1, r2, true});
  for (int r1 = 12; r1 >= 1; r1--)  // 78 offsuit
    for (int r2 = r1 - 1; r2 >= 0; r2--) types.push_back({r1, r2, false});
  return types;
}

std::vector<std::pair<std::string, float>> PreflopLUTCalculator::TopHandsByPosition(
    const PreflopLUT& lut, int num_top) {
  std::vector<std::pair<std::string, float>> ranked;
  for (int i = 0; i < PreflopLUT::NUM_TYPES; i++)
    ranked.push_back({PreflopHandType::FromIndex(i).ToString(), lut.ev_vs_1bb[i]});
  std::sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.second > b.second; });
  if (ranked.size() > static_cast<size_t>(num_top)) ranked.resize(num_top);
  return ranked;
}

}  // namespace phase10
}  // namespace poker_engine
