#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace poker_engine::game {

// All monetary values are in CENTS (1 chip = 1 cent).
// Display formatting converts to dollars at the boundary.
using Chips = int64_t;
static constexpr Chips kCentsPerDollar = 100;

// Convert dollars (double) to cents (int64_t) for backward-compatible API inputs.
inline constexpr Chips ToCents(double dollars) {
  return static_cast<Chips>(dollars * kCentsPerDollar + 0.5);
}
inline constexpr double ToDollars(Chips cents) {
  return static_cast<double>(cents) / kCentsPerDollar;
}

enum class ActionType : uint8_t {
  FOLD = 0,
  CHECK = 1,
  CALL = 2,
  BET = 3,
  RAISE = 4,
  ALL_IN = 5,
  POST_SB = 6,
  POST_BB = 7,
  POST_ANTE = 8,
  SIT_DOWN = 9,
  STAND_UP = 10,
  SIT_OUT = 11
};

struct GameAction {
  ActionType type;
  Chips amount = 0;
  int32_t player_id = 0;
  int16_t street = 0;

  std::string ToString() const;
  bool IsValid() const;
};

struct RaiseDetails {
  Chips previous_bet = 0;
  Chips new_bet = 0;
  Chips raise_amount = 0;
  Chips min_raise = 0;
  Chips max_raise = 0;
};

enum class BettingRound : uint8_t {
  PREFLOP = 0,
  FLOP = 1,
  TURN = 2,
  RIVER = 3,
  SHOWDOWN = 4,
  COMPLETE = 5
};

inline const char* BettingRoundName[] = {"Preflop", "Flop",     "Turn",
                                         "River",   "Showdown", "Complete"};

enum class SeatState : uint8_t {
  EMPTY = 0,
  SITTING = 1,
  PLAYING = 2,
  FOLDED = 3,
  ALL_IN = 4,
  SITTING_OUT = 5
};

enum class GamePhase : uint8_t {
  WAITING = 0,
  DEALING = 1,
  PREFLOP_BETTING = 2,
  FLOP_DEALING = 3,
  FLOP_BETTING = 4,
  TURN_DEALING = 5,
  TURN_BETTING = 6,
  RIVER_DEALING = 7,
  RIVER_BETTING = 8,
  SHOWDOWN = 9,
  PAYOUT = 10,
  HAND_COMPLETE = 11,
  ERROR = 99
};

// Avoid unused warning via inline
inline const char* GamePhaseName[] = {"Waiting",
                                      "Dealing",
                                      "Preflop",
                                      "Flop Deal",
                                      "Flop Betting",
                                      "Turn Deal",
                                      "Turn Betting",
                                      "River Deal",
                                      "River Betting",
                                      "Showdown",
                                      "Payout",
                                      "Complete",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "Error"};

struct CommunityCards {
  uint8_t cards[5];
  uint8_t count;

  CommunityCards() : count(0) { std::fill(cards, cards + 5, 0xFF); }
  void Reset() {
    count = 0;
    std::fill(cards, cards + 5, 0xFF);
  }
  void Add(uint8_t card_id) {
    if (count < 5) cards[count++] = card_id;
  }
  bool HasFlop() const { return count >= 3; }
  bool HasTurn() const { return count >= 4; }
  bool HasRiver() const { return count >= 5; }
  std::string ToString() const;
  std::string CardsStr() const;
};

enum class GameType : uint8_t { NLHE = 0, PLO = 1, PLO5 = 2, PLO_HILO = 3 };

struct HoleCards {
  std::array<uint8_t, 4> cards = {0xFF, 0xFF, 0xFF, 0xFF};
  int count = 0;   // 0=not dealt, 2=NLHE, 4=Omaha
  bool dealt = false;

  HoleCards() = default;
  // NLHE: 2 cards
  void Set(uint8_t c1, uint8_t c2) {
    cards = {c1, c2, 0xFF, 0xFF}; count = 2; dealt = true;
  }
  // Omaha: 4 cards
  void SetOmaha(uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4) {
    cards = {c1, c2, c3, c4}; count = 4; dealt = true;
  }
  void Reset() {
    cards = {0xFF, 0xFF, 0xFF, 0xFF}; count = 0; dealt = false;
  }
  bool IsDealt() const { return dealt; }
  // Backward compat accessors
  uint8_t card1() const { return cards[0]; }
  uint8_t card2() const { return cards[1]; }
  std::vector<uint8_t> ToVector() const {
    return std::vector<uint8_t>(cards.begin(), cards.begin() + count);
  }
  std::string ToString() const;
};

struct WinResult {
  int32_t player_id = 0;
  Chips amount = 0;
  std::string hand_name;
  int hand_rank = 0;
  std::string ToString() const;
};

}  // namespace poker_engine::game
