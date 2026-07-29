#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace poker_engine {

// ==================== 基础类型定义 ====================

using CardId = uint8_t;      // 0-51
using HandId_t = uint16_t;   // 0-1325 (C(52,2))
using AbstractId = uint8_t;  // 0-168 (169 abstract hands)
using Street = uint8_t;      // 0=Preflop, 3=Flop, 4=Turn, 5=River
using ActionIndex = uint8_t;

// ==================== 花色枚举 ====================

enum class Suit : uint8_t { Clubs = 0, Hearts = 1, Diamonds = 2, Spades = 3 };

// ==================== 排名枚举 ====================

enum class Rank : uint8_t {
  Deuce = 0,
  Trey = 1,
  Four = 2,
  Five = 3,
  Six = 4,
  Seven = 5,
  Eight = 6,
  Nine = 7,
  Ten = 8,
  Jack = 9,
  Queen = 10,
  King = 11,
  Ace = 12
};

// 手牌: 2 张牌用于起手范围
constexpr int CARDS_PER_HAND = 2;
constexpr int CARDS_FOR_EVAL = 7;
constexpr int CARDS_FOR_5 = 5;
constexpr int RANKS = 13;
constexpr int SUITS = 4;
constexpr int CARDS_PER_DECK = 52;
constexpr int TOTAL_COMBOS = 1326;
constexpr int ABSTRACT_COMBOS = 169;

constexpr CardId CARD_NONE = 255;

// ==================== 牌型分类 ====================

enum class HandCategory : uint8_t {
  HighCard = 1,
  OnePair = 2,
  TwoPair = 3,
  ThreeOfAKind = 4,
  Straight = 5,
  Flush = 6,
  FullHouse = 7,
  FourOfAKind = 8,
  StraightFlush = 9
};

inline const char* CategoryName(HandCategory cat) {
  switch (cat) {
    case HandCategory::HighCard:
      return "High Card";
    case HandCategory::OnePair:
      return "One Pair";
    case HandCategory::TwoPair:
      return "Two Pair";
    case HandCategory::ThreeOfAKind:
      return "Three of a Kind";
    case HandCategory::Straight:
      return "Straight";
    case HandCategory::Flush:
      return "Flush";
    case HandCategory::FullHouse:
      return "Full House";
    case HandCategory::FourOfAKind:
      return "Four of a Kind";
    case HandCategory::StraightFlush:
      return "Straight Flush";
    default:
      return "Unknown";
  }
}

inline const char* RankChar(Rank r) {
  static const char* names[] = {"2", "3", "4", "5", "6", "7", "8", "9", "T", "J", "Q", "K", "A"};
  return names[static_cast<int>(r)];
}

inline const char* SuitChar(Suit s) {
  static const char* names[] = {"c", "h", "d", "s"};
  return names[static_cast<int>(s)];
}

// ==================== 序列化文件头 ====================

struct FileHeader {
  static constexpr uint32_t MAGIC_V1 = 0x504B524E;
};

struct RangeFileHeader {
  uint32_t magic = 0;
  uint32_t version = 0;
  uint32_t payload_size = 0;
  uint32_t num_entries = 0;
  uint32_t flags = 0;
  bool Valid() const { return magic == FileHeader::MAGIC_V1; }
};

// NOTE: EvalResult is defined in poker_engine/evaluator/evaluator.h
// Do not add a duplicate definition here.

}  // namespace poker_engine
