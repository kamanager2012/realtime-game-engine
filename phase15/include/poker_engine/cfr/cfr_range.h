#pragma once

#include <algorithm>
#include <array>
#include <numeric>
#include <string>
#include <vector>

#include "cfr_abstraction.h"
#include "types.h"

namespace poker_engine::cfr {

class Range {
 public:
  Range();

  static Range FullRange();
  static Range TopPercent(const HandAbstraction& abstraction, double percent);
  static Range FromString(const std::string& range_str, const HandAbstraction& abstraction);

  double GetBucket(uint16_t bucket) const;
  void SetBucket(uint16_t bucket, double weight);

  void Normalize();
  double TotalWeight() const;
  double HandCount() const;
  double ComboCount() const;

  Range Intersect(const Range& other) const;
  Range Exclude(const Range& other) const;
  Range Union(const Range& other) const;

  Range& operator*=(double scalar);
  Range& operator+=(const Range& other);
  Range& operator-=(const Range& other);
  Range operator*(double scalar) const;
  Range operator+(const Range& other) const;
  Range operator-(const Range& other) const;

  bool IsHandInRange(HoleCards hand, const HandAbstraction& abstraction) const;
  std::string ToString(const HandAbstraction& abstraction) const;

  static constexpr int kBuckets = HandAbstraction::kNumBuckets;

 private:
  std::array<double, kBuckets> weights_;
};

class RangeBuilder {
 public:
  RangeBuilder(const HandAbstraction& abstraction);

  RangeBuilder& Add(const std::string& group);
  RangeBuilder& Remove(const std::string& group);
  RangeBuilder& Add(const std::string& group, double weight);
  RangeBuilder& TopPercent(double percent);
  Range Build() const;

 private:
  const HandAbstraction& abstraction_;
  Range range_;

  void ParseGroup(const std::string& group);
  void AddPair(const std::string& rank_str, double weight);
  void AddSuited(const std::string& rank_str, double weight);
  void AddOffsuit(const std::string& rank_str, double weight);
  void AddBroadwaySuited(double weight);
  void AddAllSuited(double weight);

  static int RankCharToIndex(char c);
  static std::string NormalizeGroup(const std::string& group);
};

double RangeOverlap(const Range& a, const Range& b);

Range RangeFromEquity(const std::vector<std::pair<uint16_t, double>>& equity_map, double threshold);

}  // namespace poker_engine::cfr
