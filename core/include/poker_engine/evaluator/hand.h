#pragma once
#include <array>
#include <stdexcept>

#include "poker_engine/evaluator/card.h"

namespace poker_engine {
namespace evaluator {

template <size_t N>
class HandBase {
  static_assert(N >= 2 && N <= 7);
  std::array<Card, N> cards_{};

 public:
  HandBase() = default;
  explicit HandBase(std::initializer_list<Card> list) {
    if (list.size() != N) throw std::invalid_argument("Wrong number of cards");
    std::copy(list.begin(), list.end(), cards_.begin());
  }
  Card& operator[](size_t i) { return cards_[i]; }
  const Card& operator[](size_t i) const { return cards_[i]; }
  size_t Size() const { return N; }
  bool IsValid() const {
    for (size_t i = 0; i < N; i++)
      for (size_t j = i + 1; j < N; j++)
        if (cards_[i] == cards_[j]) return false;
    return true;
  }
};

using HoleCards = HandBase<2>;
using FiveCards = HandBase<5>;
using SevenCards = HandBase<7>;

}  // namespace evaluator
}  // namespace poker_engine
