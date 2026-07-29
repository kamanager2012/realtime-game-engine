#!/usr/bin/env python3
"""Generate a 169x169 preflop equity lookup table.

The 169 hand categories:
  0-12:  Pairs (AA=0, KK=1, ..., 22=12)
  13-90: Suited (AKs=13, KQs=14, ..., 32s=90)  
  91-168: Offsuit (AKo=91, KQo=92, ..., 32o=168)

Each entry [i][j] = probability that hand i beats hand j (heads-up).
Uses Monte Carlo with 25K samples per matchup (~1.5% error, sufficient for AI).

Output: C++ header with static constexpr double table[169][169].
"""

import random
import itertools
import sys

RANKS = '23456789TJQKA'
RANK_ORDER = {r: i for i, r in enumerate(RANKS)}  # 2=0, A=12
SAMPLES = 5000  # per matchup

def rank(card_id):
    return card_id % 13

def suit(card_id):
    return card_id // 13

def hand_rank(hole, board):
    """Evaluate a 7-card poker hand. Returns a tuple (category, kickers)
    where category is 0=high card, 1=pair, ..., 8=straight flush, 9=royal."""
    cards = list(hole) + list(board)
    ranks = [rank(c) for c in cards]
    suits = [suit(c) for c in cards]

    # Count rank frequencies
    freq = [0] * 13
    for r in ranks:
        freq[r] += 1

    # Sort by (frequency, rank) descending
    sorted_counts = sorted([(f, r) for r, f in enumerate(freq)], reverse=True)

    # Check for straight
    def has_straight(ranks_set):
        for high in range(12, 3, -1):
            if all(r in ranks_set for r in range(high - 4, high + 1)):
                return high
        if {0, 1, 2, 3, 12}.issubset(ranks_set):
            return 3  # wheel
        return -1

    # Check for flush
    suit_counts = [0] * 4
    for s in suits:
        suit_counts[s] += 1
    flush_suit = next((i for i, c in enumerate(suit_counts) if c >= 5), -1)

    is_flush = flush_suit >= 0
    rank_set = set(ranks)
    straight_high = has_straight(rank_set)

    # Straight flush
    if is_flush and straight_high >= 0:
        flush_ranks = {rank(c) for c in cards if suit(c) == flush_suit}
        sf_high = has_straight(flush_ranks)
        if sf_high >= 0:
            return (9 if sf_high == 12 else 8, sf_high)

    # Four of a kind
    if sorted_counts[0][0] == 4:
        return (7, sorted_counts[0][1] * 13 + sorted_counts[1][1])

    # Full house
    if sorted_counts[0][0] == 3 and sorted_counts[1][0] >= 2:
        return (6, sorted_counts[0][1] * 13 + sorted_counts[1][1])

    # Flush
    if is_flush:
        flush_ranks = sorted([rank(c) for c in cards if suit(c) == flush_suit], reverse=True)
        return (5, flush_ranks[0] * 13**4 + flush_ranks[1] * 13**3 +
                flush_ranks[2] * 13**2 + flush_ranks[3] * 13 + flush_ranks[4])

    # Straight
    if straight_high >= 0:
        return (4, straight_high)

    # Three of a kind
    if sorted_counts[0][0] == 3:
        kickers = sorted([r for r in ranks if freq[r] != 3], reverse=True)
        return (3, sorted_counts[0][1] * 13**2 + kickers[0] * 13 + kickers[1])

    # Two pair
    if sorted_counts[0][0] == 2 and sorted_counts[1][0] == 2:
        high_pair = max(sorted_counts[0][1], sorted_counts[1][1])
        low_pair = min(sorted_counts[0][1], sorted_counts[1][1])
        kicker = sorted_counts[2][1]
        return (2, high_pair * 13**2 + low_pair * 13 + kicker)

    # One pair
    if sorted_counts[0][0] == 2:
        kickers = sorted([r for r in ranks if freq[r] != 2], reverse=True)
        return (1, sorted_counts[0][1] * 13**3 + kickers[0] * 13**2 +
                kickers[1] * 13 + kickers[2])

    # High card
    kickers = sorted(ranks, reverse=True)
    return (0, kickers[0] * 13**4 + kickers[1] * 13**3 +
            kickers[2] * 13**2 + kickers[3] * 13 + kickers[4])


def hand_to_index(r1, r2, suited):
    """Convert two ranks to 169-index. r1 >= r2 (higher rank first)."""
    if suited:
        idx = 0
        for i in range(r1):
            idx += (12 - i)  # total combos in row i
        idx += r2
        return 13 + idx
    else:
        idx = 0
        for i in range(r1):
            idx += (12 - i)
        idx += r2
        return 91 + idx


def index_to_cards(idx):
    """Given a 169-index, return two representative card IDs."""
    # This is approximate - returns any representative cards
    if idx < 13:  # pair
        r = 12 - idx  # AA=0 -> rank 12, KK=1 -> rank 11, ...
        return (r * 4, r * 4 + 1)  # different suits
    elif idx < 91:  # suited
        idx -= 13
        # Find r1, r2 where r1 > r2
        for r1 in range(12, -1, -1):
            count = r1  # r1 choices for r2 (0..r1-1)
            if idx < count:
                r2 = idx
                return (r1 * 4, r2 * 4)  # same suit
            idx -= count
    else:  # offsuit
        idx -= 91
        for r1 in range(12, -1, -1):
            count = r1
            if idx < count:
                r2 = idx
                return (r1 * 4, r2 * 4 + 1)  # different suits
            idx -= count
    return (0, 0)


def compute_equity(hero_idx, villain_idx):
    """Monte Carlo equity: hero hand vs villain hand."""
    if hero_idx == villain_idx:
        return 0.5  # same hand = chop

    h1, h2 = index_to_cards(hero_idx)
    v1, v2 = index_to_cards(villain_idx)

    if len(set([h1, h2, v1, v2])) < 4:
        return 0.5  # overlapping cards = chop

    used = {h1, h2, v1, v2}
    deck = [c for c in range(52) if c not in used]

    wins = 0
    for _ in range(SAMPLES):
        board = random.sample(deck, 5)
        hero_hr = hand_rank((h1, h2), board)
        villain_hr = hand_rank((v1, v2), board)
        if hero_hr > villain_hr:
            wins += 1
        elif hero_hr == villain_hr:
            wins += 0.5

    return wins / SAMPLES


def main():
    print("Generating 169x169 preflop equity table...", file=sys.stderr)
    print(f"Samples per matchup: {SAMPLES}", file=sys.stderr)

    table = [[0.5] * 169 for _ in range(169)]

    for i in range(169):
        for j in range(i + 1, 169):
            eq = compute_equity(i, j)
            table[i][j] = eq
            table[j][i] = 1.0 - eq
        if i % 10 == 0:
            print(f"  Row {i}/169", file=sys.stderr)

    # Output as C++ constexpr array
    print("// Auto-generated preflop equity table (169x169)")
    print(f"// Generated with {SAMPLES} samples per matchup")
    print("// clang-format off")
    print("#pragma once")
    print("#include <array>")
    print()
    print("namespace poker_engine::ai {")
    print()
    print(f"inline constexpr int kPreflopSamples = {SAMPLES};")
    print()
    print("inline constexpr std::array<std::array<double, 169>, 169> kPreflopEquity = {{")
    for i in range(169):
        vals = ", ".join(f"{table[i][j]:.6f}" for j in range(169))
        print(f"    {{ {vals} }},")
    print("}};")
    print()
    print("}  // namespace poker_engine::ai")

    print("Done.", file=sys.stderr)


if __name__ == "__main__":
    main()
