# Architecture

How the pieces fit. For the trust model see [SECURITY.md](SECURITY.md),
[FAIRNESS.md](FAIRNESS.md), [THREAT_MODEL.md](THREAT_MODEL.md).

## Layers

```
cli/                Server binaries
├── poker_ws_server.cpp   WebSocket + HTTP server (single binary)
├── http_router.*         HTTP routing (extracted module)
└── server_runtime.*      config loading (env: ports, origins, limits)

phase12/           Core game engine (the deterministic runtime)
├── game_state.cpp        state machine: ProcessAction, start/hand lifecycle
├── dealer.cpp            provably-fair shuffle (CSPRNG + PRF)
├── pot_manager.cpp       side-pot construction (int64)
├── showdown_evaluator.cpp pot split (int64, remainder distribution)
├── action_validator.cpp  bet/raise/call validation, rejects negatives
└── player_state.cpp      per-seat chip state

phase13/           Networking, auth, AI
├── auth_service.cpp      PBKDF2, JWT, revocation
├── session_manager.cpp   CSPRNG session tokens
├── game_server.cpp       table orchestration, bot hooks
└── ai_engine.cpp         rule-based + CFR bot decisions

phase14/           Persistence
├── account_repository.cpp  parameterized account SQL
├── chip_ledger.cpp         authoritative int64 ledger + Reconcile()
├── hand_repository.cpp     hand history
└── wallet.cpp              transaction records

phase16-17/       Anti-cheat (ML + heuristic), per-hand
phase15/          CFR training & tournaments
phase20/          Preflop equity table
phase21/          Observability (logging / metrics / tracing)
core/             Foundation: hand evaluator, ranges
equity/           Monte-Carlo + exact equity
infra/net/        Redis client + distributed session store
frontend/         React 18 + TypeScript 5 (Vite)
deploy/          Docker, K8s, Vault configs
```

## Request / data flow

1. **Connect** — client opens a WebSocket. Auth token arrives via the
   `Authorization: Bearer` header or the WS subprotocol (never the URL). The
   server validates the JWT, binds `player_id` to the socket, and checks the
   `Origin` header.
2. **Subscribe / sit** — client subscribes to a table; `Table Actor` tracks
   seats, turn order, and per-player action timeout.
3. **Act** — `GameState::ProcessAction` (mutex-guarded) validates the action
   with `ActionValidator`, mutates `Chips`, updates the pot, and emits events.
4. **Settle** — at showdown, `ShowdownEvaluator` splits the int64 pot exactly;
   `PlayerState::Receive` credits winners; `ChipLedger` records the movement.
5. **Record** — every action and outcome is appended to the event log
   (replay source) and the hand history; `ChipLedger::Reconcile()` can later
   prove balances match the transaction log.
6. **Anti-cheat** — after each hand, the anti-cheat manager analyzes the
   snapshot; on `Confirmed` it bans (revokes tokens, tears down live sockets,
   persists `bans.json`).

## Concurrency model

- One game table is mutated under its own `GameState` mutex; actions are
  serialized per table.
- The WebSocket server is single-threaded epoll-style dispatch with a
  `clients` registry guarded by a recursive mutex; long work (hand analysis)
  is handed to the anti-cheat thread.
- `ChipLedger` uses optimistic locking (`UPDATE ... WHERE chips = ?`) so
  concurrent balance mutations can't lose updates.

## Why it is game-agnostic

`GameState` owns a generic seat/action/turn state machine and an integer chip
model. Poker-specific rules (blind structure, showdown) are one implementation
of the game interface; another game (chess clocks, betting rounds, etc.) plugs
into the same state machine, ledger, replay, and anti-cheat substrate.
