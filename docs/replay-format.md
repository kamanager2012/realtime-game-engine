# Replay Format

The engine records **every state transition as an event**. A hand is fully
deterministic given the event stream (plus the RNG seed/nonce for any deal), so
any hand can be replayed for arbitration, cheat investigation, or AI analysis.

## Why event sourcing

- **Arbitration** — a disputed hand is reproducible exactly; no trusting the
  server's word, just the log.
- **Cheat investigation** — anti-cheat consumes the same stream; collusion and
  timing patterns are visible in the events.
- **Audit** — the ledger and the event log are reconciled; if they diverge,
  something is wrong.

## Event shape

Every event is a structured record with at least:

```
seq_id      : monotonically increasing per stream
table_id    : which table/game
hand_id     : which hand (groups events for one deal)
type        : e.g. player_joined, action, deal, showdown, payout, disconnect
actor       : player_id (or "system")
payload     : type-specific fields (the action taken, cards if revealed, deltas)
ts          : server timestamp (for display only; not used in game logic)
```

Money-affecting events also carry the **chip delta** and the resulting balance,
so the ledger can be rebuilt from the event stream alone.

## Replay procedure

1. Load the hand's events in `seq_id` order.
2. Re-apply each `action` event to a fresh state machine.
3. At `deal`/`showdown` events, verify the revealed cards match the committed
   RNG (`SHA256(seed ‖ nonce)`) — this proves the deal was not altered.
4. Compare the final state (and payouts) to the recorded result.

Because `ProcessAction` is deterministic and money is integer, the replayed
final state **must** equal the original. A mismatch is a hard integrity failure.

## Storage

Events are append-only. The structured audit logger
(`phase21/structured_audit_logger.cpp`) writes them with sequence numbers and
per-entry integrity; the ledger (`wallet_transactions`) mirrors the
money-affecting subset. `ChipLedger::Reconcile()` cross-checks the two.
