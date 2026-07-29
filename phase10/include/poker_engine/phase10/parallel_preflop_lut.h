#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "poker_engine/evaluator/card.h"
#include "poker_engine/phase10/parallel_utils.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase10 {

struct PreflopHandType {
  int rank1 = 0;
  int rank2 = 0;
  bool suited = false;
  std::string ToString() const;
  static int16_t ToIndex(int r1, int r2, bool suited);
  static PreflopHandType FromIndex(int16_t idx);
  static PreflopHandType FromHandName(const std::string& name);
};

struct PreflopLUT {
  static constexpr int NUM_TYPES = 169;
  std::array<std::array<uint16_t, NUM_TYPES>, NUM_TYPES> equity_vs_type{};
  std::array<float, NUM_TYPES> equity_vs_random{};
  std::array<float, NUM_TYPES> equity_vs_1bbrange{};
  std::array<float, NUM_TYPES> ev_vs_1bb{};

  void SaveToFile(const std::string& filepath) const;
  bool LoadFromFile(const std::string& filepath);
  std::string ToString() const;

  float GetEquity(int hero_idx, int villain_idx) const {
    return equity_vs_type[hero_idx][villain_idx] / 65535.0f;
  }
  void SetEquity(int hero_idx, int villain_idx, float eq) {
    equity_vs_type[hero_idx][villain_idx] =
        static_cast<uint16_t>(std::clamp(eq, 0.0f, 1.0f) * 65535);
  }
};

class PreflopLUTCalculator {
 public:
  explicit PreflopLUTCalculator(int num_threads = -1);
  PreflopLUT Calculate(int mc_samples_per_matchup = 2000);
  bool Load(const std::string& filepath);
  static float QueryEquity(const PreflopLUT& lut, const std::string& a, const std::string& b);
  static std::vector<std::pair<std::string, float>> TopHandsByPosition(const PreflopLUT& lut,
                                                                       int num_top = 30);

 private:
  int num_threads_;
  static std::vector<PreflopHandType> GenerateAllHandTypes();
};

}  // namespace phase10
}  // namespace poker_engine
