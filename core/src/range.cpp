#include "poker_engine/range/range.h"

#include <algorithm>
#include <cctype>

#include "poker_engine/base/assertion.h"
#include "poker_engine/evaluator/card.h"

namespace poker_engine {
namespace range {
namespace {

inline int CharToRank(char c) {
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

enum class HandType { Pocket, Suited, Offsuit, Either };

struct ParsedHand {
  int r1 = -1, r2 = -1;
  HandType type = HandType::Either;
};

// Parse a single hand token (e.g. "AKs", "TT", "97o").
// Returns false (and leaves `out` untouched) on malformed input so the
// caller can skip it gracefully instead of aborting or producing garbage.
bool ParseHand(const std::string& s, ParsedHand& out) {
  if (s.length() < 2) {
    PE_LOG_ERROR("Range parse: token '{}' too short", s);
    return false;
  }
  int a = CharToRank(s[0]), b = CharToRank(s[1]);
  if (a < 0 || b < 0) {
    PE_LOG_ERROR("Range parse: token '{}' has invalid rank", s);
    return false;
  }
  if (a == b) {
    out.r1 = a;
    out.r2 = b;
    out.type = HandType::Pocket;
    return true;
  }
  if (s.length() < 3) {
    out.r1 = std::max(a, b);
    out.r2 = std::min(a, b);
    out.type = HandType::Either;
    return true;
  }
  out.r1 = std::max(a, b);
  out.r2 = std::min(a, b);
  if (s[2] == 's' || s[2] == 'S')
    out.type = HandType::Suited;
  else if (s[2] == 'o' || s[2] == 'O')
    out.type = HandType::Offsuit;
  else {
    PE_LOG_ERROR("Range parse: token '{}' has invalid modifier", s);
    return false;
  }
  return true;
}

inline void AddSuited(Range& r, int hi, int lo) {
  for (int s = 0; s < 4; s++) r.Set(HandId::Encode(hi * 4 + s, lo * 4 + s), 1);
}
inline void AddOffsuit(Range& r, int hi, int lo) {
  for (int s1 = 0; s1 < 4; s1++)
    for (int s2 = 0; s2 < 4; s2++)
      if (s1 != s2) r.Set(HandId::Encode(hi * 4 + s1, lo * 4 + s2), 1);
}
inline void AddPairs(Range& r, int rank) {
  for (int s1 = 0; s1 < 4; s1++)
    for (int s2 = s1 + 1; s2 < 4; s2++) r.Set(HandId::Encode(rank * 4 + s1, rank * 4 + s2), 1);
}

void SetToken(Range& r, const std::string& tok) {
  if (tok.empty()) return;

  // Check for "+" suffix
  bool plus = (tok.back() == '+');
  std::string base = plus ? tok.substr(0, tok.size() - 1) : tok;

  // Check for "-" range
  size_t dash = base.find('-');
  if (dash != std::string::npos) {
    ParsedHand from, to;
    if (!ParseHand(base.substr(0, dash), from) || !ParseHand(base.substr(dash + 1), to)) {
      PE_LOG_ERROR("Range parse: skipping malformed range token '{}'", tok);
      return;
    }
    // A dash range must span the same hand shape: pair-to-pair, or
    // same suited/offsuit high card (e.g. AKs-AJs). Reject mismatches.
    bool from_pair = (from.r1 == from.r2);
    bool to_pair = (to.r1 == to.r2);
    if (from_pair != to_pair) {
      PE_LOG_ERROR("Range parse: mismatched pair/non-pair in '{}'", tok);
      return;
    }
    if (!from_pair && from.r1 != to.r1) {
      PE_LOG_ERROR("Range parse: dash range '{}' must share high card", tok);
      return;
    }
    if (from_pair) {
      for (int rank = from.r1; rank >= to.r1 && rank >= 0; rank--) AddPairs(r, rank);
    } else {
      for (int rl = from.r2; rl >= to.r2 && rl >= 0; rl--) {
        if (from.type == HandType::Suited || from.type == HandType::Either)
          AddSuited(r, from.r1, rl);
        if (from.type == HandType::Offsuit || from.type == HandType::Either)
          AddOffsuit(r, from.r1, rl);
      }
    }
    return;
  }

  // Single hand token
  ParsedHand h;
  if (!ParseHand(base, h)) {
    PE_LOG_ERROR("Range parse: skipping malformed token '{}'", tok);
    return;
  }

  if (plus) {
    if (h.r1 == h.r2) {
      // Pocket pair plus: QQ+ → QQ, KK, AA
      for (int rank = h.r1; rank <= 12; rank++) AddPairs(r, rank);
    } else {
      // Non-pair plus: KTs+ → KTs, KJs, KQs, AKs
      for (int rl = h.r2; rl < h.r1; rl++) {
        if (h.type == HandType::Suited || h.type == HandType::Either) AddSuited(r, h.r1, rl);
        if (h.type == HandType::Offsuit || h.type == HandType::Either) AddOffsuit(r, h.r1, rl);
      }
    }
    return;
  }

  // No plus, no dash — single hand
  if (h.r1 == h.r2) {
    AddPairs(r, h.r1);
  } else {
    if (h.type == HandType::Suited || h.type == HandType::Either) AddSuited(r, h.r1, h.r2);
    if (h.type == HandType::Offsuit || h.type == HandType::Either) AddOffsuit(r, h.r1, h.r2);
  }
}

void Trim(std::string& s) {
  s.erase(s.begin(),
          std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
  s.erase(
      std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
      s.end());
}

}  // namespace
}  // namespace range
}  // namespace poker_engine

poker_engine::range::Range poker_engine::range::Range::FromString(const std::string& range_str) {
  Range r;
  std::istringstream iss(range_str);
  std::string token;
  while (std::getline(iss, token, ',')) {
    Trim(token);
    if (token.empty()) continue;
    SetToken(r, token);
  }
  return r;
}
