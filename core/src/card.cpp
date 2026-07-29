#include "poker_engine/evaluator/card.h"

#include <stdexcept>

namespace poker_engine {

Card Card::Parse(const std::string& s) {
  if (s.length() < 2) throw std::invalid_argument("Invalid card: " + s);
  char rc = s[0], sc = s[1];
  Rank r;
  Suit sv;
  switch (rc) {
    case '2':
      r = Rank::Deuce;
      break;
    case '3':
      r = Rank::Trey;
      break;
    case '4':
      r = Rank::Four;
      break;
    case '5':
      r = Rank::Five;
      break;
    case '6':
      r = Rank::Six;
      break;
    case '7':
      r = Rank::Seven;
      break;
    case '8':
      r = Rank::Eight;
      break;
    case '9':
      r = Rank::Nine;
      break;
    case 'T':
      r = Rank::Ten;
      break;
    case 'J':
      r = Rank::Jack;
      break;
    case 'Q':
      r = Rank::Queen;
      break;
    case 'K':
      r = Rank::King;
      break;
    case 'A':
      r = Rank::Ace;
      break;
    default:
      throw std::invalid_argument(std::string("Bad rank: ") + rc);
  }
  switch (sc) {
    case 'c':
      sv = Suit::Clubs;
      break;
    case 'h':
      sv = Suit::Hearts;
      break;
    case 'd':
      sv = Suit::Diamonds;
      break;
    case 's':
      sv = Suit::Spades;
      break;
    default:
      throw std::invalid_argument(std::string("Bad suit: ") + sc);
  }
  return Card(r, sv);
}

std::string Card::ToString() const {
  std::string s;
  s += RankChar(GetRank());
  s += SuitChar(GetSuit());
  return s;
}

}  // namespace poker_engine
