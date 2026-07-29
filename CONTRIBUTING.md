# Contributing to realtime-game-engine

Thanks for your interest in contributing. This project is a **production-oriented
C++20 real-time multiplayer game engine**; poker (Texas Hold'em / Omaha) is the
reference implementation. The engine is game-agnostic at its core, so most
contributions are about the runtime, not the game.

## Code Style

- **C++20** with `-Wall -Wextra -Wpedantic`
- No exceptions for flow control — prefer `std::optional` / `std::expected`
- Prefer `std::unique_ptr` over raw/shared ownership
- `const` correctness on all methods
- Namespaces: `poker_engine::<domain>` (e.g. `poker_engine::game`, `poker_engine::ai`)

## Development Setup

```bash
# Requirements: C++20 compiler (GCC 11+ / Clang 14+), CMake 3.16+,
# OpenSSL (libssl-dev), SQLite3 (libsqlite3-dev). All other deps vendored.

cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/tests/poker_tests          # 405 tests, all must pass

# Frontend (React/TS) — source only; build output is git-ignored
cd frontend && npm install && npm run build

# Run the server locally
./build/cli/poker_ws_server 9001
```

Optional: `pip install websocket-client` to run `scripts/benchmark_connections.py`.

## Architecture Guide

Start with [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (module map + data flow)
and [docs/DESIGN.md](docs/DESIGN.md) (system design). The core layers:

```
Client → Gateway (cli/poker_ws_server) → Table Runtime → Game State Core
        → { Replay · Ledger · AI/Solver · Pot/Showdown } → Anti-Cheat
```

The **Game State Core** (`phase12/`) is a deterministic state machine with
integer (`Chips`/`int64`) money math. Anything that changes player balances or
hand outcomes must go through it and be recorded by the event log + ledger.

## Module Ownership

| Area | Location | Notes |
|------|----------|-------|
| Game state machine, showdown, pot | `phase12/` | Deterministic core; no floating-point money |
| Dealer / provably-fair RNG | `phase12/src/dealer.cpp` | 256-bit seed, SHA-256 commit-reveal, HMAC PRF |
| Chip ledger | `phase14/` + ledger module | `int64` balances, `Reconcile()` audit |
| Anti-cheat | `phase19/` + `phase21/` | Real-time per-hand analysis → warn/kick/ban |
| Networking / gateway | `cli/`, `infra/net/` | WS + HTTP, subprotocol auth, CORS, rate-limit |
| Auth | `phase13/` | PBKDF2 600k, JWT HS256, revocation |
| AI / Solver | `phase15/`, `phase18/`, `phase20/` | CFR, equity, preflop tables |
| Frontend | `frontend/` | React/TS client |

## How to Add a Game (extension guide)

The engine is built so a *different* turn-based game can run on the same
runtime. The reference is poker; see [docs/game-plugin-guide.md](docs/game-plugin-guide.md)
for the contract a game implementation must satisfy:

1. Define your `Action` set and validation rules.
2. Implement a deterministic state machine that takes `(state, action) → state`
   using integer math, and emits **events** for every transition.
3. Plug your evaluator / scoring into the showdown step.
4. Emit the same event types the replay + ledger + anti-cheat layers already
   consume, so arbitration, audit, and cheat detection keep working.

You do **not** need to touch the gateway, ledger, replay, or anti-cheat code to
add a game — that is the point of the runtime.

## Pull Request Process

1. Fork the repository
2. Create a feature branch
3. Add tests for new functionality (unit +, where relevant, property/fuzz tests)
4. Ensure all tests pass (`./build/tests/poker_tests`)
5. Submit a PR with a clear description of the change and its threat-model impact
   (if it touches money, auth, or transport)

## Areas for Contribution

- **Runtime**: connection fan-out, backpressure, horizontal scaling (multi-node)
- **AI**: CFR training improvements, new bot strategies, equity analysis
- **Variants**: Short Deck (6+), Pineapple, 5-Card Omaha
- **Frontend**: UX improvements, mobile support, accessibility
- **Performance**: evaluation lookup tables, protocol optimization
- **Testing**: fuzzing harnesses (`tests/fuzz/`), property-based tests
  (`tests/property_tests.cpp`), load testing (`scripts/benchmark_connections.py`)

## Security

If you discover a security vulnerability, do **not** open a public issue. See
[docs/SECURITY.md](docs/SECURITY.md) for the reporting policy. The project's
trust model (fair RNG, ledger integrity, auditable transport) is documented in
[docs/FAIRNESS.md](docs/FAIRNESS.md) and [docs/THREAT_MODEL.md](docs/THREAT_MODEL.md).
