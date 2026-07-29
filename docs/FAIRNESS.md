# Provably-Fair RNG

The shuffle is designed so that **neither the operator nor a player can
predict or bias the deck**, and any past hand can be independently verified.
This is the "provably fair" property that most open-source poker projects lack.

## Properties guaranteed

1. **Unpredictability before the deal** — the deck is a function of a 256-bit
   secret seed drawn from the OS CSPRNG (`RAND_bytes`). The seed is never
   exposed before the hand completes.
2. **Non-malleability** — a commitment is published *before* the deal that
   binds the operator to a specific seed without revealing it.
3. **Verifiability** — after the hand, the seed and nonce are revealed and
   anyone can recompute the exact deck and confirm it matches the commitment
   and the played-out hand.
4. **Sufficient entropy** — the shuffle expansion uses the full 256-bit seed
   (≈ 2²⁵⁶ keyspace), not a truncated value.

## Protocol

```
Setup (per hand)
  seed   ← RAND_bytes(32)              // 256-bit, secret until reveal
  nonce  ← RAND_bytes(32)              // fresh random salt
  commitment = SHA256(seed ‖ nonce)    // published BEFORE the deal

Deal
  stream = HMAC-SHA256(key = seed, msg = "fy" ‖ nonce ‖ block_index)
  for i = 51 down to 1:
      j = uniform(0 .. i) via rejection sampling over `stream`
      swap(deck[i], deck[j])

Reveal (after hand)
  publish (seed, nonce)
  verifier checks:
      SHA256(seed ‖ nonce) == commitment
      HMAC-SHA256(seed, "fy"‖nonce‖*) reproduces the exact deck
```

`Dealer::GetProof()` returns the full audit record
(`commitment`, `seed_hex`, `nonce`, `deck_hash`) for storage alongside the hand
history.

## Why HMAC-SHA256 PRF (and not `mt19937`)

A Fisher-Yates over a `std::mt19937` seeded from only 64 bits of entropy would
have a keyspace of ≈ 2⁶⁴ — below the 128-bit security floor and not
"provably fair" in the cryptographic sense, even though the seed stays secret
during live play. We instead expand the **full 256-bit seed** through
HMAC-SHA256: each Fisher-Yates index is drawn from a CSPRNG-quality stream
keyed by the secret seed. This keeps the same deterministic, replayable
behavior the audit trail needs while meeting the entropy bar.

Rejection sampling (`limit = 2³² − (2³² mod n)`) removes modulo bias, so each
index is uniformly distributed over `[0, i]`.

## What this does NOT guarantee

- **Operator collusion with a player in real time** is prevented (the seed is
  secret and committed before any card is seen). However, the current
  commitment is server-only: the *player* does not contribute a seed, so a
  fully trusted setup would add a client seed to the commitment. This is a
  documented enhancement, not a live exploit.
- Fairness of the *game rules* (payout logic) is covered separately by the
  integer money math and `ChipLedger::Reconcile()` (see
  [SECURITY.md](SECURITY.md) and the `Showdown_PotSplitConservation` test).

## Verifying a hand yourself

1. Read `commitment`, `seed_hex`, `nonce`, `deck_hash` from the stored
   `HandProof`.
2. Confirm `SHA256(seed ‖ nonce) == commitment`.
3. Re-run the HMAC-SHA256 PRF Fisher-Yates with `(seed, nonce)` and confirm
   the resulting deck hashes to `deck_hash` and matches the dealt cards.
