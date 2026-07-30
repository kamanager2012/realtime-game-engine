# PokerEngine — A C++20 Real-Time Multiplayer Game Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Build](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Tests](https://img.shields.io/badge/tests-405%2F405-brightgreen)]()

> **A production-oriented C++20 real-time multiplayer game engine**, featuring
> deterministic state transitions, provably-fair RNG, event sourcing, ledger
> accounting, and anti-cheat infrastructure. **Poker (Texas Hold'em / Omaha) is
> the reference implementation** — the engine is game-agnostic at its core.
>
> **~72,000 lines of C++20** (engine + game, excluding vendored dependencies),
> plus a TypeScript frontend — a real, auditable, low-level real-time system,
> not a Python demo.

> **Not a real-money / gambling platform.** This project provides infrastructure
> for real-time multiplayer games. It does not provide gambling services or
> payment processing. Run it as a free-to-play or skill-based environment under
> the regulations that apply to you.

This is not a poker demo. It is a reusable runtime for stateful, real-time,
money-relevant multiplayer games: a deterministic game state machine, a
tamper-evident chip ledger, a verifiable shuffle, full event replay, and
real-time anti-cheat — composed so that any turn-based game can be hosted on
the same infrastructure.

## Why this exists

Most open-source game backends stop at "a database column for the balance."
This project treats the server as a **financial system**:

```
deterministic Game State ─┐
provably-fair RNG         ─┤
event-sourcing / replay  ─┼──►  a trustworthy real-time game runtime
chip Ledger + Reconcile  ─┤
real-time anti-cheat     ─┘
```

The scarce, defensible assets are **not** the solver — they are fairness,
ledger integrity, and auditability. Those are what make the engine reusable
across games and trustworthy enough for money-relevant play.

## Feature map

| Capability | What it gives you |
|------------|-------------------|
| **Deterministic state machine** | `GameState::ProcessAction` with exact integer (`Chips`/int64) money math, mutex-guarded, timeout/auto-fold |
| **Provably-fair RNG** | 256-bit CSPRNG seed, SHA-256 commitment-reveal, HMAC-SHA256 PRF Fisher-Yates (2²⁵⁶ entropy) — auditable post-hoc |
| **Chip Ledger** | authoritative int64 balance store, optimistic-lock concurrency guard, `Reconcile()` audit, append-only `wallet_transactions` |
| **Event sourcing / Replay** | every hand serialized as events; full deterministic replay for arbitration, cheat investigation, AI analysis |
| **Anti-cheat** | real-time per-hand analysis, collusion/timing detection, graduated warn→kick→ban with immediate connection teardown |
| **Networking** | self-contained WebSocket + HTTP server, subprotocol auth (no token in URL), CORS allowlist, Origin check, rate limiting, body/frame size caps |
| **Auth** | PBKDF2-HMAC-SHA256 (600k iters), constant-time compare, JWT HS256 with revocation |
| **Persistence** | SQLite WAL + optional PostgreSQL mirror, fully parameterized SQL |
| **AI / Analysis** | rule-based bots, CFR, exact + Monte-Carlo equity, precomputed 169×169 preflop table |
| **Ops** | Docker / K8s manifests, Vault integration, Prometheus metrics, structured JSON logging, graceful shutdown + crash recovery |

### Reference game: Poker

The bundled reference game is a full Texas Hold'em (NLHE) and Omaha (PLO4/PLO5)
engine: blinds, antes, side pots, all-in, showdown with exact chip-conserving
split. Plus matchmaking, spectator mode, leaderboards, and a React/TypeScript
frontend.

> **Note on framing:** this is published as *real-time multiplayer game
> infrastructure*; poker is the first proof case. It is not positioned as, or
> endorsed for, a real-money gambling operation — run it as a free-to-play or
> skill-based environment under the regulations that apply to you.

## Architecture

```
            Client (browser / bot)
                │
         Gateway Layer  (cli/poker_ws_server)
         WebSocket + HTTP · subprotocol auth · CORS · Origin · rate-limit
                │
         Table Runtime  (per-table Actor: seats, turn order, action timeout)
                │
         Game State Core  (phase12 — deterministic state machine, int64 chips)
         ┌───────┼────────┬──────────┐
         ▼       ▼        ▼          ▼
     Replay   Ledger   AI / Solver   Pot / Showdown
    (events) (int64,   (bots, CFR,   (exact split)
              Reconcile) equity)
                │
         Anti-Cheat  (real-time per-hand analysis → warn / kick / ban)
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full module map and
data-flow, and [docs/DESIGN.md](docs/DESIGN.md) for system design.

## Security & trust

The engine ships with three dedicated documents — read these first if you
care about deploying it:

- [docs/SECURITY.md](docs/SECURITY.md) — hardening summary, reporting policy
- [docs/FAIRNESS.md](docs/FAIRNESS.md) — provably-fair shuffle & verifiability
- [docs/THREAT_MODEL.md](docs/THREAT_MODEL.md) — assumed adversary & mitigations

Headline guarantees (full adversarial audit, 0 Critical / 0 High / 0 Medium
outstanding):

- **RNG** — 256-bit CSPRNG seed, SHA-256 commitment-reveal, HMAC-SHA256 PRF
  expansion; deck replayable and verifiable from `(seed, nonce)`.
- **Finance** — integer `Chips` (int64) everywhere; `ChipLedger::Reconcile()`
  detects any drift between balances and the signed transaction log.
- **Auth** — PBKDF2 600k iters, constant-time comparison, JWT revocation.
- **Transport** — auth token carried via `Authorization` header / WS
  subprotocol, never the URL query (no proxy-log leakage).
- **SQL** — 100% parameterized.

## Quick Start

```bash
# Build (C++20, CMake 3.16+, OpenSSL, SQLite3; rest vendored in third_party/)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run tests (405 tests)
./build/tests/poker_tests

# Start server
./build/cli/poker_ws_server 9001

# Or with Docker
docker compose -f deploy/docker-compose.yml up -d
```

## Demo

The fastest way to see the engine run end-to-end (server + 6 auto-playing bots
+ replayable hands) is Docker:

```bash
# 1. Start the stack (WS/HTTP server, optional Redis, DB)
docker compose -f deploy/docker-compose.yml up -d

# 2. Open the web client
#    http://localhost:8080  (served by the frontend container)
#
# 3. Create a table / room from the lobby, then seat 6 bots.
#    The bots auto-play Texas Hold'em; each completed hand is written to the
#    event log and can be replayed deterministically from its (seed, nonce).

# 4. Headless smoke test (no browser needed):
python3 scripts/ws_smoke_test.py --host localhost --port 9001 --bots 6
```

What you will observe:

- **Deterministic state machine** driving the table; every action is an event.
- **Provably-fair shuffle** — open the replay to see the commitment and the
  verifiable deck for any hand.
- **Ledger** — open the server logs / `wallet_transactions` to confirm every
  chip movement reconciles exactly (`Reconcile()` reports zero drift).
- **Anti-cheat** — suspicious patterns trigger graduated warn → kick → ban with
  immediate WebSocket teardown.

> **Omaha (PLO4/5/6) is opt-in.** The large pre-generated Omaha lookup tables
> are omitted from the repository to keep clone size small. Texas Hold'em —
> the reference implementation — needs none of them and works out of the box.
> To enable Omaha, run `scripts/gen_omaha_tables.sh` and reconfigure.

## AI / Agent Research

The engine doubles as a **reproducible research environment for
imperfect-information, multi-agent decision making**. Agents implement a single
interface (`IAIEngine::Decide`) and are evaluated on the real game — exact
side-pot settlement, provably-fair dealing, integer chip accounting — not a toy
abstraction. `Decide` receives a **redacted per-player `Observation`**: opponents'
hole cards are not exposed at the type level, only the viewer's own cards.

`agent_bench` runs headless bot-vs-bot matches and reports a win rate (mbb/100)
with a 95% confidence interval, asserting chip conservation every hand:

```bash
# random vs random → no edge (CI crosses zero)
./build/cli/agent_bench --a random --b random --hands 20000 --seed 1

# rule-based vs random → clear, significant edge (lower CI bound > 0)
./build/cli/agent_bench --a rule --b random --hands 20000 --seed 1

# duplicate (seat-rotated) dealing cancels card luck → much tighter CI
./build/cli/agent_bench --a rule --b random --hands 4000 --seed 1 --duplicate

# all-in EV adjustment: score all-in runouts by exact equity → tighter CI (heads-up)
./build/cli/agent_bench --a rule --b random --hands 4000 --seed 1 --aivat

# N-way (>2) match, parallelized across threads for throughput
./build/cli/agent_bench --agents random,rule,cfr --hands 20000 --threads 8 \
    --cfr-model data/bot_policy.cfr
```

Baselines: `RandomAgent` (honest floor), rule-based, and a CFR solver policy.
See **[docs/ai-research.md](docs/ai-research.md)** for the agent API, how to add
your own agent, metric definitions, and honest limitations.

## Building

### Requirements
- **C++20** compiler (GCC 11+ or Clang 14+)
- **CMake 3.16+**, **OpenSSL** (`libssl-dev`), **SQLite3** (`libsqlite3-dev`)

All other dependencies are vendored in `third_party/`: nlohmann/json, spdlog,
fmt, Eigen3, PokerHandEvaluator, GoogleTest.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug                 # debug
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON # + sanitizers
cmake -B build -DOFFLINE_BUILD=ON                        # offline
```

## Documentation

| Document | Description |
|----------|-------------|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Module map & data flow |
| [docs/DESIGN.md](docs/DESIGN.md) | Full system design |
| [docs/SECURITY.md](docs/SECURITY.md) | Security hardening & reporting |
| [docs/FAIRNESS.md](docs/FAIRNESS.md) | Provably-fair RNG design |
| [docs/THREAT_MODEL.md](docs/THREAT_MODEL.md) | Adversary model & mitigations |
| [docs/game-plugin-guide.md](docs/game-plugin-guide.md) | How to add a new game on the runtime |
| [docs/replay-format.md](docs/replay-format.md) | Event-sourcing / replay contract |
| [docs/ledger-design.md](docs/ledger-design.md) | Integer chip ledger & `Reconcile()` |
| [docs/ALGORITHMS.md](docs/ALGORITHMS.md) | Algorithm inventory |
| [docs/ai-research.md](docs/ai-research.md) | Agent API, bot-vs-bot benchmark & metrics |
| [docs/adr/](docs/adr/) | Architecture Decision Records |

## License

MIT — see [LICENSE](LICENSE).
