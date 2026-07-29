#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "poker_engine/base/logging.h"
#include "poker_engine/base/serialization.h"
#include "poker_engine/base/types.h"
#include "poker_engine/range/hand_id.h"
#include "poker_engine/range/suit_iso.h"

namespace poker_engine {
namespace range {

class Range {
  static constexpr int TOTAL_COMBOS = 1326;
  std::array<float, TOTAL_COMBOS> probs_{};

 public:
  Range() = default;
  static Range Uniform() {
    Range r;
    r.probs_.fill(1.0f);
    return r;
  }
  static Range FullCombinatorial() { return Uniform(); }
  static Range FromString(const std::string& range_str);
  // LoadBinary is inline inside the class (no separate declaration)

  float Get(uint16_t id) const { return probs_[id]; }
  void Set(uint16_t id, float v) { probs_[id] = v; }
  float& operator[](uint16_t id) { return probs_[id]; }
  const float& operator[](uint16_t id) const { return probs_[id]; }
  const std::array<float, TOTAL_COMBOS>& Data() const { return probs_; }

  Range& operator&=(const Range& o) {
    for (size_t i = 0; i < TOTAL_COMBOS; i++) probs_[i] = std::min(probs_[i], o.probs_[i]);
    return *this;
  }
  Range& operator|=(const Range& o) {
    for (size_t i = 0; i < TOTAL_COMBOS; i++) probs_[i] = std::max(probs_[i], o.probs_[i]);
    return *this;
  }
  Range& operator+=(const Range& o) {
    for (size_t i = 0; i < TOTAL_COMBOS; i++) probs_[i] += o.probs_[i];
    return *this;
  }
  Range& operator-=(const Range& o) {
    for (size_t i = 0; i < TOTAL_COMBOS; i++) probs_[i] = std::max(0.f, probs_[i] - o.probs_[i]);
    return *this;
  }
  Range& operator*=(const Range& o) {
    for (size_t i = 0; i < TOTAL_COMBOS; i++) probs_[i] *= o.probs_[i];
    return *this;
  }
  Range& operator*=(float s) {
    for (auto& v : probs_) v *= s;
    return *this;
  }

  Range& RemoveHand(uint8_t c1, uint8_t c2) {
    probs_[HandId::Encode(c1, c2)] = 0;
    return *this;
  }
  Range& RemoveCard(uint8_t card) {
    for (uint8_t c = 0; c < 52; c++) {
      if (c == card) continue;
      uint8_t lo = c < card ? c : card, hi = c < card ? card : c;
      probs_[HandId::Encode(lo, hi)] = 0;
    }
    return *this;
  }
  Range& RemoveBoard(const std::vector<uint8_t>& board) {
    for (auto c : board) RemoveCard(c);
    return *this;
  }

  int NonZeroCount() const {
    int n = 0;
    for (auto v : probs_)
      if (v > 1e-8f) n++;
    return n;
  }
  float Sum() const {
    float s = 0;
    for (auto v : probs_) s += v;
    return s;
  }
  float Max() const {
    float m = 0;
    for (auto v : probs_)
      if (v > m) m = v;
    return m;
  }
  float Entropy() const {
    float total = Sum();
    if (total < 1e-12f) return 0;
    float e = 0;
    for (auto v : probs_)
      if (v > 1e-12f) {
        float p = v / total;
        e -= p * std::log(p);
      }
    return e;
  }
  float EffectiveCombos() const {
    float s = Sum(), ss = 0;
    for (auto v : probs_) ss += v * v;
    return ss > 1e-12f ? s * s / ss : 0;
  }

  Range& Normalize() {
    float t = Sum();
    if (t > 1e-12f)
      for (auto& v : probs_) v /= t;
    return *this;
  }

  uint16_t Sample(std::mt19937& rng) const {
    float total = Sum();
    std::uniform_real_distribution<float> ud(0, total);
    float target = ud(rng), cs = 0;
    for (int i = 0; i < TOTAL_COMBOS; i++) {
      cs += probs_[i];
      if (cs >= target) return i;
    }
    for (int i = TOTAL_COMBOS - 1; i >= 0; i--)
      if (probs_[i] > 0) return i;
    return 0;
  }

  // Optimized sampling: caller provides the already-computed total (Sum) and a
  // reusable distribution object, avoiding per-call recomputation/allocation in
  // tight Monte Carlo loops.
  uint16_t SampleWithTotal(std::mt19937& rng, float total,
                            std::uniform_real_distribution<float>& ud) const {
    (void)total;  // `total` sizes `ud` at the call site; unused here.
    float target = ud(rng), cs = 0;
    for (int i = 0; i < TOTAL_COMBOS; i++) {
      cs += probs_[i];
      if (cs >= target) return i;
    }
    for (int i = TOTAL_COMBOS - 1; i >= 0; i--)
      if (probs_[i] > 0) return i;
    return 0;
  }

  std::array<float, SuitIsomorphism::ABSTRACT_HANDS> CollapseToAbstract() const {
    std::array<float, SuitIsomorphism::ABSTRACT_HANDS> a{};
    a.fill(0);
    for (int i = 0; i < TOTAL_COMBOS; i++) {
      if (probs_[i] == 0) continue;
      auto [c1, c2] = HandId::Decode(i);
      a[SuitIsomorphism::ToAbstract(c1, c2)] += probs_[i];
    }
    return a;
  }

  std::string Summary() const {
    std::ostringstream oss;
    oss << "Range(hands=" << NonZeroCount() << " sum=" << Sum() << " entropy=" << Entropy() << ")";
    return oss.str();
  }

  // FIX: inline - no separate declaration vs definition conflict
  bool SaveBinary(const std::string& path) const {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;
    base::RangeFileHeader h;
    h.magic = base::FileHeader::MAGIC_V1;
    h.version = 1;
    h.payload_size = sizeof(float) * TOTAL_COMBOS;
    h.num_entries = TOTAL_COMBOS;
    if (!base::WriteBinary(ofs, h)) return false;
    return base::WriteArray(ofs, probs_.data(), TOTAL_COMBOS);
  }

  static Range LoadBinary(const std::string& path) {
    Range r;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
      PE_LOG_ERROR("Cannot open: {}", path);
      return r;
    }
    base::RangeFileHeader h;
    if (!base::ReadBinary(ifs, h) || !h.Valid()) {
      PE_LOG_ERROR("Invalid header: {}", path);
      return Range();
    }
    if (!base::ReadArray(ifs, r.probs_.data(), h.num_entries)) {
      PE_LOG_ERROR("Read data failed: {}", path);
      return Range();
    }
    return r;
  }
};

inline Range operator&(Range a, const Range& b) {
  a &= b;
  return a;
}
inline Range operator|(Range a, const Range& b) {
  a |= b;
  return a;
}
inline Range operator+(Range a, const Range& b) {
  a += b;
  return a;
}
inline Range operator-(Range a, const Range& b) {
  a -= b;
  return a;
}
inline Range operator*(Range a, const Range& b) {
  a *= b;
  return a;
}
inline Range operator*(Range a, float s) {
  a *= s;
  return a;
}
inline Range operator*(float s, Range a) { return a * s; }

}  // namespace range
}  // namespace poker_engine
