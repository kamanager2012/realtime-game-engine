#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace poker_engine::cfr {

inline constexpr int kMaxHoleCards = 2;
inline constexpr int kMaxCommunityCards = 5;
inline constexpr int kTotalCards = 52;
inline constexpr int kMaxPlayers = 6;
inline constexpr int kRanks = 13;
inline constexpr int kSuits = 4;

inline constexpr int kMaxBetAbstractions = 4;
inline constexpr int kMaxRaiseAbstractions = 3;
inline constexpr int kMaxActions = 4;

struct CFRConfig {
  int num_iterations = 1000;
  double discount_interval = 1.0;
  double regret_floor = -1000.0;
  double regret_ceil = 1000.0;
  double alpha = 1.5;
  double beta = 0.5;
  double gamma = 2.0;
  double epsilon = 0.0;
  double prune_threshold = 0.0;
  double chance_sampling = 1.0;
  double exploitability_threshold = 0.01;
};

struct HoleCards {
  uint8_t c1;
  uint8_t c2;

  uint16_t encode() const { return static_cast<uint16_t>(c1) * kTotalCards + c2; }

  static HoleCards decode(uint16_t code) {
    return {static_cast<uint8_t>(code / kTotalCards), static_cast<uint8_t>(code % kTotalCards)};
  }

  bool suited() const { return (c1 / kRanks) == (c2 / kRanks); }
  bool is_pair() const { return (c1 % kRanks) == (c2 % kRanks); }
};

enum class Street : uint8_t { Preflop = 0, Flop = 1, Turn = 2, River = 3, Terminal = 4 };

struct BoardCards {
  uint8_t cards[5];
  uint8_t count = 0;

  void add_card(uint8_t card_idx) {
    if (count < 5) cards[count++] = card_idx;
  }

  void clear() {
    count = 0;
    memset(cards, -1, sizeof(cards));
  }
};

enum class Action : uint8_t { Fold = 0, Call = 1, BetHalf = 2, BetPot = 3, AllIn = 4 };

inline const char* action_name(Action a) {
  switch (a) {
    case Action::Fold:
      return "fold";
    case Action::Call:
      return "call";
    case Action::BetHalf:
      return "half_pot";
    case Action::BetPot:
      return "pot";
    case Action::AllIn:
      return "all_in";
    default:
      return "unknown";
  }
}

inline int action_count() { return 5; }

enum class NodeType : uint8_t {
  Terminal = 0,
  Chance = 1,
  PlayerDecision = 2,
  OpponentDecision = 3
};

}  // namespace poker_engine::cfr
