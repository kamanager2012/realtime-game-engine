# Ledger Design

The engine treats chip movement as a **financial system**, not a database column.
The `ChipLedger` is the authoritative, integer store of every player's balance,
and it is the module that makes the engine safe for money-relevant play.

## Core guarantees

- **Integer money only** — balances and all transfers use `Chips` (`int64_t`).
  No floating point anywhere in the money path.
- **Conservation** — for every pot, `Σ payouts == pot` exactly. Split
  remainders are distributed deterministically (winner order), so no chips are
  created or destroyed.
- **Optimistic-lock concurrency** — balance updates apply via
  `UPDATE ... SET chips = chips + ? WHERE chips = ?` so a stale read can never
  silently overwrite a newer balance.
- **Append-only transactions** — every money-affecting event is written to
  `wallet_transactions` with the delta and resulting balance.
- **Reconcile** — `ChipLedger::Reconcile()` recomputes balances from the
  transaction log and compares to the live balances; any drift is reported as a
  hard failure.

## Data flow

```
Game State Core (deterministic)
        │  emits money-affecting events (payouts, buys, etc.)
        ▼
ChipLedger.Apply(player, delta)
        │  1. optimistic-lock balance update (int64)
        │  2. append wallet_transactions row (delta, balance_after)
        ▼
persisted balances  ──┐
                      ├─► Reconcile()  (balances == Σ deltas ?)
event log            ──┘
```

## Why this matters for trust

- A bug or an attack that tries to grant chips shows up as a
  `Reconcile()` mismatch — detectable, not silent.
- Because the money path is integer and event-sourced, the ledger and the
  replay stream can be cross-checked independently (see
  [docs/replay-format.md](replay-format.md)).
- External payment integration (if ever added) would sit *outside* this core;
  the ledger is the source of truth the engine trusts.

## Operating notes

- The ledger is the bottleneck you want: it serializes per account, which is
  correct for money safety. Throughput scales by having many independent
  accounts (many tables/players), not by relaxing the lock.
- `Reconcile()` should be run periodically (and on startup) as an integrity
  gate; a non-zero drift is an incident, not a warning.
