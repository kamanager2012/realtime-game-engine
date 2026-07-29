#include "poker_engine/cfr/cfr_range.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "poker_engine/cfr/cfr_abstraction.h"

namespace poker_engine::cfr {

Range::Range() { weights_.fill(0.0); }

Range Range::FullRange() {
  Range r;
  r.weights_.fill(1.0);
  return r;
}

Range Range::TopPercent(const HandAbstraction& abstraction, double percent) {
  Range r;
  int limit = static_cast<int>(Range::kBuckets * percent / 100.0);
  for (int i = 0; i < limit && i < Range::kBuckets; ++i) {
    r.weights_[i] = 1.0;
  }
  return r;
}

Range Range::FromString(const std::string& range_str, const HandAbstraction& abstraction) {
  return RangeBuilder(abstraction).Add(range_str).Build();
}

double Range::GetBucket(uint16_t bucket) const {
  if (bucket >= kBuckets) return 0.0;
  return weights_[bucket];
}

void Range::SetBucket(uint16_t bucket, double weight) {
  if (bucket < kBuckets) weights_[bucket] = weight;
}

void Range::Normalize() {
  double total = TotalWeight();
  if (total > 0) {
    for (auto& w : weights_) w /= total;
  }
}

double Range::TotalWeight() const {
  double total = 0.0;
  for (auto w : weights_) total += w;
  return total;
}

double Range::HandCount() const { return TotalWeight() * 4.0; }
double Range::ComboCount() const { return HandCount(); }

Range Range::Intersect(const Range& other) const {
  Range result;
  for (int i = 0; i < kBuckets; ++i) result.weights_[i] = std::min(weights_[i], other.weights_[i]);
  return result;
}

Range Range::Exclude(const Range& other) const {
  Range result;
  for (int i = 0; i < kBuckets; ++i)
    result.weights_[i] = std::max(0.0, weights_[i] - other.weights_[i]);
  return result;
}

Range Range::Union(const Range& other) const {
  Range result;
  for (int i = 0; i < kBuckets; ++i) result.weights_[i] = std::max(weights_[i], other.weights_[i]);
  return result;
}

Range& Range::operator*=(double scalar) {
  for (auto& w : weights_) w *= scalar;
  return *this;
}

Range& Range::operator+=(const Range& other) {
  for (int i = 0; i < kBuckets; ++i) weights_[i] += other.weights_[i];
  return *this;
}

Range& Range::operator-=(const Range& other) {
  for (int i = 0; i < kBuckets; ++i) weights_[i] = std::max(0.0, weights_[i] - other.weights_[i]);
  return *this;
}

Range Range::operator*(double scalar) const {
  Range r = *this;
  return r *= scalar;
}
Range Range::operator+(const Range& other) const {
  Range r = *this;
  return r += other;
}
Range Range::operator-(const Range& other) const {
  Range r = *this;
  return r -= other;
}

bool Range::IsHandInRange(HoleCards hand, const HandAbstraction& abstraction) const {
  uint16_t bucket = abstraction.get_bucket(hand);
  return weights_[bucket] > 0;
}

std::string Range::ToString(const HandAbstraction& abstraction) const {
  std::ostringstream oss;
  bool first = true;
  for (int i = 0; i < kBuckets; ++i) {
    if (weights_[i] > 0.5) {
      if (!first) oss << ", ";
      oss << abstraction.bucket_name(i);
      if (weights_[i] < 1.0) oss << "(" << weights_[i] << ")";
      first = false;
    }
  }
  return oss.str();
}

double RangeOverlap(const Range& a, const Range& b) {
  double overlap = 0.0;
  double total = 0.0;
  for (int i = 0; i < Range::kBuckets; ++i) {
    overlap += std::min(a.GetBucket(i), b.GetBucket(i));
    total += std::max(a.GetBucket(i), b.GetBucket(i));
  }
  return total > 0 ? overlap / total : 0.0;
}

Range RangeFromEquity(const std::vector<std::pair<uint16_t, double>>& equity_map,
                      double threshold) {
  Range r;
  for (auto& [bucket, equity] : equity_map) {
    if (equity >= threshold) r.SetBucket(bucket, equity);
  }
  return r;
}

// ==================== RangeBuilder ====================

RangeBuilder::RangeBuilder(const HandAbstraction& abstraction) : abstraction_(abstraction) {}

RangeBuilder& RangeBuilder::Add(const std::string& group) {
  ParseGroup(group);
  return *this;
}

RangeBuilder& RangeBuilder::Remove(const std::string& group) { return *this; }

RangeBuilder& RangeBuilder::Add(const std::string& group, double weight) {
  ParseGroup(group);
  return *this;
}

RangeBuilder& RangeBuilder::TopPercent(double percent) {
  range_ = Range::TopPercent(abstraction_, percent);
  return *this;
}

Range RangeBuilder::Build() const { return range_; }

void RangeBuilder::ParseGroup(const std::string& group) {
  std::string g = group;
  g.erase(std::remove_if(g.begin(), g.end(), ::isspace), g.end());
  if (g.empty()) return;

  if (g == "AA") {
    AddPair("AA", 1.0);
  } else if (g.find("s") != std::string::npos && g.find("o") == std::string::npos) {
    std::string ranks = g;
    ranks.erase(std::remove(ranks.begin(), ranks.end(), 's'), ranks.end());
    AddSuited(ranks, 1.0);
  } else if (g.find("o") != std::string::npos) {
    std::string ranks = g;
    ranks.erase(std::remove(ranks.begin(), ranks.end(), 'o'), ranks.end());
    AddOffsuit(ranks, 1.0);
  } else if (g.size() == 2 && g[0] == g[1]) {
    AddPair(g, 1.0);
  } else if (g.size() == 2) {
    AddSuited(g, 1.0);
    AddOffsuit(g, 1.0);
  } else if (g.find("broadway") != std::string::npos) {
    AddBroadwaySuited(1.0);
  } else if (g == "any" || g == "100%") {
    range_ = Range::FullRange();
  }
}

void RangeBuilder::AddPair(const std::string& rank_str, double weight) {
  if (rank_str.size() < 2) return;
  int r = RankCharToIndex(rank_str[0]);
  if (r < 0) return;

  for (int s1 = 0; s1 < kSuits; ++s1) {
    for (int s2 = s1 + 1; s2 < kSuits; ++s2) {
      uint8_t c1 = static_cast<uint8_t>(r + s1 * kRanks);
      uint8_t c2 = static_cast<uint8_t>(r + s2 * kRanks);
      if (c1 > c2) std::swap(c1, c2);
      HoleCards hc{c1, c2};
      uint16_t bucket = abstraction_.get_bucket(hc);
      range_.SetBucket(bucket, std::max(range_.GetBucket(bucket), weight));
    }
  }
}

void RangeBuilder::AddSuited(const std::string& rank_str, double weight) {
  if (rank_str.size() < 2) return;
  int r1 = RankCharToIndex(rank_str[0]);
  int r2 = RankCharToIndex(rank_str[1]);
  if (r1 < 0 || r2 < 0) return;

  for (int s = 0; s < kSuits; ++s) {
    uint8_t c1 = static_cast<uint8_t>(r1 + s * kRanks);
    uint8_t c2 = static_cast<uint8_t>(r2 + s * kRanks);
    if (c1 > c2) std::swap(c1, c2);
    HoleCards hc{c1, c2};
    uint16_t bucket = abstraction_.get_bucket(hc);
    range_.SetBucket(bucket, std::max(range_.GetBucket(bucket), weight));
  }
}

void RangeBuilder::AddOffsuit(const std::string& rank_str, double weight) {
  if (rank_str.size() < 2) return;
  int r1 = RankCharToIndex(rank_str[0]);
  int r2 = RankCharToIndex(rank_str[1]);
  if (r1 < 0 || r2 < 0) return;

  for (int s1 = 0; s1 < kSuits; ++s1) {
    for (int s2 = 0; s2 < kSuits; ++s2) {
      if (s1 == s2) continue;
      uint8_t c1 = static_cast<uint8_t>(r1 + s1 * kRanks);
      uint8_t c2 = static_cast<uint8_t>(r2 + s2 * kRanks);
      if (c1 > c2) std::swap(c1, c2);
      HoleCards hc{c1, c2};
      uint16_t bucket = abstraction_.get_bucket(hc);
      range_.SetBucket(bucket, std::max(range_.GetBucket(bucket), weight));
    }
  }
}

void RangeBuilder::AddBroadwaySuited(double weight) {
  const char* broadway = "TJQKA";
  for (int i = 0; broadway[i]; ++i) {
    for (int j = i + 1; broadway[j]; ++j) {
      AddSuited(std::string() + broadway[i] + broadway[j], weight);
    }
  }
}

int RangeBuilder::RankCharToIndex(char c) {
  switch (c) {
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
}

std::string RangeBuilder::NormalizeGroup(const std::string& group) {
  std::string g = group;
  g.erase(std::remove_if(g.begin(), g.end(), ::isspace), g.end());
  return g;
}

}  // namespace poker_engine::cfr
