#include "poker_engine/equity/equity_calculator.h"

#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>

#include "poker_engine/base/types.h"
#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/hand_id.h"

namespace poker_engine {
namespace equity {
namespace {

using poker_engine::range::Range;

inline int Evaluate7Cards(const uint8_t c1a, const uint8_t c2a, const uint8_t c1b,
                          const uint8_t c2b, const uint8_t board[5]) {
  using EV = poker_engine::evaluator::Evaluator;
  uint8_t h1[7] = {c1a, c2a, board[0], board[1], board[2], board[3], board[4]};
  uint8_t h2[7] = {c1b, c2b, board[0], board[1], board[2], board[3], board[4]};
  auto r1 = EV::Evaluate7(h1);
  auto r2 = EV::Evaluate7(h2);
  if (r1 > r2) return 1;
  if (r2 > r1) return -1;
  return 0;
}

}  // namespace

EquityResult EquityCalculator::CalculateMonteCarlo(const Range& r1, const Range& r2,
                                                   const uint8_t board[], int board_size,
                                                   int n_samples, std::mt19937& rng) {
  int64_t wins[2] = {0, 0}, ties = 0;
  uint8_t used[52], full_board[5];

  int actual_samples = 0;
  int max_rejections = n_samples * 10;
  int rejections = 0;
  float total1 = r1.Sum(), total2 = r2.Sum();
  std::uniform_real_distribution<float> ud1(0, total1);
  std::uniform_real_distribution<float> ud2(0, total2);
  for (int s = 0; s < n_samples; s++) {
    auto [lo1, hi1] = range::HandId::Decode(r1.SampleWithTotal(rng, total1, ud1));
    auto [lo2, hi2] = range::HandId::Decode(r2.SampleWithTotal(rng, total2, ud2));
    uint8_t ca[2] = {lo1, hi1}, cb[2] = {lo2, hi2};

    if (ca[0] == cb[0] || ca[0] == cb[1] || ca[1] == cb[0] || ca[1] == cb[1]) {
      s--;
      rejections++;
      if (rejections >= max_rejections) break;
      continue;
    }
    actual_samples++;

    std::memset(used, 0, 52);
    for (int i = 0; i < board_size; i++) used[board[i]] = 1;
    used[ca[0]] = used[ca[1]] = used[cb[0]] = used[cb[1]] = 1;

    DrawBoard(full_board, board, board_size, used, 5, rng);

    int res = Evaluate7Cards(ca[0], ca[1], cb[0], cb[1], full_board);
    if (res == 1)
      wins[0]++;
    else if (res == -1)
      wins[1]++;
    else
      ties++;
  }

  EquityResult res;
  res.total_trials = actual_samples > 0 ? actual_samples : n_samples;
  int denom = actual_samples > 0 ? actual_samples : n_samples;
  res.win[0] = float(wins[0]) / denom;
  res.win[1] = float(wins[1]) / denom;
  res.tie[0] = float(ties) / denom;
  res.tie[1] = float(ties) / denom;
  res.equity[0] = res.win[0] + res.tie[0] * 0.5f;
  res.equity[1] = res.win[1] + res.tie[1] * 0.5f;
  return res;
}

std::string EquityResult::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "P1: win=" << win[0] * 100 << "% tie=" << tie[0] * 100 << "% eq=" << equity[0] * 100
      << "% | P2: win=" << win[1] * 100 << "% tie=" << tie[1] * 100 << "% eq=" << equity[1] * 100
      << "% | trials=" << total_trials;
  return oss.str();
}

int EquityCalculator::EvaluateHeadToHead(uint8_t c1a, uint8_t c2a, uint8_t c1b, uint8_t c2b,
                                         const uint8_t board[], int board_size) {
  if (board_size >= 5) {
    uint8_t fb[5];
    std::memcpy(fb, board, 5);
    return Evaluate7Cards(c1a, c2a, c1b, c2b, fb);
  }
  uint8_t used[52] = {0};
  for (int i = 0; i < board_size; i++) used[board[i]] = 1;
  used[c1a] = used[c2a] = used[c1b] = used[c2b] = 1;
  uint8_t fb[5];
  std::memcpy(fb, board, board_size);

  if (board_size == 4) {
    int64_t w1 = 0, w2 = 0;
    for (int c = 0; c < 52; c++) {
      if (used[c]) continue;
      fb[4] = c;
      int r = Evaluate7Cards(c1a, c2a, c1b, c2b, fb);
      if (r == 1)
        w1++;
      else if (r == -1)
        w2++;
    }
    return w1 > w2 ? 1 : (w2 > w1 ? -1 : 0);
  }
  return 0;
}

EquityResult EquityCalculator::CalculateExact(const Range& r1, const Range& r2,
                                              const uint8_t board[], int board_size, int samples) {
  if (samples > 0) {
    // Explicit sample count => caller wants Monte Carlo, not exhaustive.
    std::mt19937 rng(42);  // fixed seed for reproducible results
    return CalculateMonteCarlo(r1, r2, board, board_size, samples, rng);
  }

  if (board_size > 5) board_size = 5;

  uint8_t used[52] = {0};
  for (int i = 0; i < board_size; i++) used[board[i]] = 1;

  std::vector<uint16_t> h1_ids, h2_ids;
  for (int i = 0; i < 1326; i++) {
    if (r1.Get(i) > 0) h1_ids.push_back(i);
    if (r2.Get(i) > 0) h2_ids.push_back(i);
  }

  int need = 5 - board_size;

  // Guard: exhaustive enumeration blows up for very large range products.
  int64_t avail_n = 52 - board_size;
  int64_t combos = 1;
  for (int k = 0; k < need; k++) combos = combos * (avail_n - 1 - k) / (k + 1);
  int64_t total_work = combos * int64_t(h1_ids.size()) * int64_t(h2_ids.size());
  if (total_work >= 200000000LL) {
    std::mt19937 rng(42);
    return CalculateMonteCarlo(r1, r2, board, board_size, 200000, rng);
  }

  int64_t w1 = 0, w2 = 0, ties = 0, total = 0;
  uint8_t board_out[5];
  std::memcpy(board_out, board, board_size);

  if (need == 0) {
    for (uint16_t id1 : h1_ids) {
      auto [c1a, c2a] = range::HandId::Decode(id1);
      used[c1a]++;
      used[c2a]++;
      for (uint16_t id2 : h2_ids) {
        auto [c1b, c2b] = range::HandId::Decode(id2);
        if (used[c1b] || used[c2b]) continue;
        used[c1b]++;
        used[c2b]++;
        int result = EvaluateHeadToHead(c1a, c2a, c1b, c2b, board_out, 5);
        if (result == 1) w1++;
        else if (result == -1) w2++;
        else ties++;
        total++;
        used[c1b]--;
        used[c2b]--;
      }
      used[c1a]--;
      used[c2a]--;
    }
  } else {
    for (uint16_t id1 : h1_ids) {
      auto [c1a, c2a] = range::HandId::Decode(id1);
      used[c1a]++;
      used[c2a]++;
      for (uint16_t id2 : h2_ids) {
        auto [c1b, c2b] = range::HandId::Decode(id2);
        if (used[c1b] || used[c2b]) continue;
        used[c1b]++;
        used[c2b]++;

        // Cards available to complete the board: everything not already used.
        uint8_t avail[52];
        int na = 0;
        for (int c = 0; c < 52; c++)
          if (!used[c]) avail[na++] = uint8_t(c);

        std::vector<int> idx(need, 0);
        for (int i = 0; i < need; i++) idx[i] = i;
        bool more = true;
        while (more) {
          for (int k = 0; k < need; k++) board_out[board_size + k] = avail[idx[k]];
          int result = EvaluateHeadToHead(c1a, c2a, c1b, c2b, board_out, 5);
          if (result == 1) w1++;
          else if (result == -1) w2++;
          else ties++;
          total++;

          int pos = need - 1;
          while (pos >= 0 && idx[pos] == na - need + pos) pos--;
          if (pos < 0) {
            more = false;
          } else {
            idx[pos]++;
            for (int i = pos + 1; i < need; i++) idx[i] = idx[i - 1] + 1;
          }
        }

        used[c1b]--;
        used[c2b]--;
      }
      used[c1a]--;
      used[c2a]--;
    }
  }

  EquityResult res;
  res.total_trials = uint64_t(total);
  if (total) {
    res.win[0] = float(w1) / total;
    res.win[1] = float(w2) / total;
    res.tie[0] = float(ties) / total;
    res.tie[1] = float(ties) / total;
    res.equity[0] = res.win[0] + res.tie[0] * 0.5f;
    res.equity[1] = res.win[1] + res.tie[1] * 0.5f;
  }
  return res;
}

EquityResult EquityCalculator::CalculatePreflopMC(int n_samples, std::mt19937& rng) {
  Range full = Range::FullCombinatorial();
  uint8_t board[5] = {0};
  return CalculateMonteCarlo(full, full, board, 0, n_samples, rng);
}

void EquityCalculator::DrawBoard(uint8_t board_out[], const uint8_t partial[], int partial_size,
                                 uint8_t used[52], int target_size, std::mt19937& rng) {
  std::memcpy(board_out, partial, partial_size);
  uint8_t rem[52];
  int rc = 0;
  for (int c = 0; c < 52; c++)
    if (!used[c]) rem[rc++] = c;
  // Reuse a single distribution object across the draw loop instead of
  // constructing a fresh one on every iteration.
  std::uniform_int_distribution<int> d(0, 51);
  for (int i = target_size - 1; i >= partial_size; i--) {
    int idx = d(rng) % rc;
    board_out[i] = rem[idx];
    rem[idx] = rem[--rc];
  }
}

}  // namespace equity
}  // namespace poker_engine
