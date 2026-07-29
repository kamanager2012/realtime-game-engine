# Game Plugin Guide

The engine is a **game-agnostic real-time runtime**; poker is the reference
implementation. This guide explains the contract a new turn-based game must
satisfy to run on the same infrastructure (gateway, ledger, replay, anti-cheat)
without modifying them.

## The invariant

Every state transition in a game must be:

1. **Deterministic** — `apply(state, action) -> state` is a pure function of
   its inputs. No hidden global state, no wall-clock-dependent branching in the
   core. This is what makes replay and arbitration possible.
2. **Integer money** — all balances/chips use `Chips` (`int64_t`). Never
   `double`. Split remainders are distributed deterministically so
   `Σ payouts == pot` always.
3. **Event-emitting** — every transition appends an event to the event log.

## What you implement

| Component | Responsibility |
|-----------|----------------|
| `Action` | The game's move set; must be (de)serializable to JSON for the wire. |
| `ActionValidator` | Legality check for `(state, action)`; rejects malformed/illegal moves. |
| `GameState` / state machine | `ProcessAction(state, action) -> new_state`, timeout/auto-fold, turn order. |
| `ShowdownEvaluator` | Scoring + exact pot split (integer conservation). |
| `Dealer` (optional) | If randomness is needed, use the provably-fair RNG (see below). |

## What you get for free

- **Transport**: the gateway (`cli/poker_ws_server`) already speaks
  `subscribe` / `action` / `table_state` over WebSocket with subprotocol auth.
  Your client speaks the same frames.
- **Ledger**: every chip movement flows through `ChipLedger` with an
  optimistic-lock guard and `Reconcile()` audit. You call
  `ledger.Apply(player, delta)`; the engine guarantees conservation.
- **Replay**: because transitions are events, any hand can be replayed
  deterministically from `(seed, nonce)` + the event stream.
- **Anti-cheat**: per-hand analysis (collusion, timing, abnormal win rates)
  operates on the event stream, so it works for your game too.

## Provably-fair RNG (if your game deals cards / hidden state)

Do **not** use `std::mt19937` or any small-entropy generator. Use the engine's
`Dealer`:

- A 256-bit seed from a CSPRNG (`OpenSSL RAND_bytes`).
- A SHA-256 **commitment-reveal**: publish `commitment = SHA256(seed ‖ nonce)`
  before the hand; reveal `seed` after, so the deal is verifiable and
  unmanipulable.
- Deck shuffling uses an HMAC-SHA256 **PRF** (keyed by the full seed) with
  bias-free rejection sampling (Fisher-Yates). The deal is reproducible from
  `(seed, nonce)`.

## Minimal checklist

- [ ] `Action` set defined + JSON (de)serialization
- [ ] Deterministic state machine (no float money, no hidden state)
- [ ] `ActionValidator` rejects illegal moves
- [ ] `ShowdownEvaluator` returns integer splits that conserve the pot
- [ ] Every transition emits an event
- [ ] (If dealing) uses `Dealer` commitment-reveal + PRF shuffle
- [ ] Unit tests for the state machine + a property test for money conservation

Once these hold, your game runs behind the same gateway/ledger/replay/anti-cheat
as poker.
