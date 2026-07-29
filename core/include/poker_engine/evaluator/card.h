#pragma once
#include <cstdint>
#include <string>

#include "poker_engine/base/types.h"

namespace poker_engine {

class Card {
  uint8_t card_id_ = 0;

 public:
  constexpr Card() = default;
  constexpr Card(Rank r, Suit s) : card_id_((uint8_t(r) << 2) | uint8_t(s)) {}
  explicit constexpr Card(uint8_t id) : card_id_(id) {}

  static Card FromId(uint8_t id) { return Card(id); }
  static Card Parse(const std::string& s);

  constexpr uint8_t Id() const { return card_id_; }
  constexpr Rank GetRank() const { return Rank(card_id_ >> 2); }
  constexpr Suit GetSuit() const { return Suit(card_id_ & 3); }
  constexpr uint8_t RankIndex() const { return card_id_ >> 2; }
  constexpr uint8_t SuitIndex() const { return card_id_ & 3; }

  std::string ToString() const;

  constexpr bool operator==(const Card& o) const { return card_id_ == o.card_id_; }
  constexpr bool operator!=(const Card& o) const { return card_id_ != o.card_id_; }
};

}  // namespace poker_engine
