#pragma once

#include <optional>
#include <string>

namespace poker_engine::cli {

struct BuyInRange {
  double min_buy_in = 10.0;
  double max_buy_in = 200.0;
};

inline std::optional<std::string> ValidateBuyInAmount(double buy_in, const BuyInRange& range) {
  if (buy_in <= 0) return "invalid_buy_in";
  if (buy_in < range.min_buy_in) return "buy_in_below_minimum";
  if (buy_in > range.max_buy_in) return "buy_in_above_maximum";
  return std::nullopt;
}

}  // namespace poker_engine::cli
