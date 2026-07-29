# Benchmark

Real numbers depend on your hardware and deployment. This document gives the
**method** and the **harness** so results are reproducible and comparable.

## What to measure

For a real-time game backend, the metrics that matter:

| Metric | Why |
|--------|-----|
| Concurrent WebSocket connections | capacity / fan-out |
| Connection establishment latency | join experience |
| Action round-trip latency (p50/p99) | gameplay feel |
| Sustained message throughput (resp/s) | headroom under load |

## Harness

`scripts/benchmark_connections.py` registers N players, opens N WebSocket
connections using the **same subprotocol auth path the browser uses**,
subscribes each to a table, and reports the metrics above.

```bash
# terminal 1: start the server
./build/cli/poker_ws_server 9001

# terminal 2: run the benchmark
pip install websocket-client
python3 scripts/benchmark_connections.py --host localhost:9001 \
    --clients 1000 --duration 30
```

The harness prints a `BENCHMARK RESULT` block with connection count, connect
latency, throughput, and p50/p95/p99 round-trip times.

## Recommended methodology

1. Run on the **target** hardware (the box you'll deploy), not a laptop.
2. Warm up, then measure for ≥ 30 s at each concurrency level
   (e.g., 200 / 1000 / 2000 / 4000 / 6000).
3. Put a reverse proxy (nginx/Envoy) + TLS in front for a realistic number;
   record both raw and proxied.
4. Report p99, not just averages — tail latency is what players feel.
5. Keep CPU/mem/idle-latency alongside; note the saturation point.

## Reporting format (fill in after a run)

```
Hardware : <CPU / cores / RAM / network>
Config   : Release build, <proxy?>, <DB: SQLite/PG>

concurrent connections : <N>
connect p99            : <x> ms
throughput            : <y> resp/s
action rtt p99        : <z> ms
CPU @ saturation      : <%>
```

> Tip: the engine's per-table mutex serializes actions *per table*. To stress
> aggregate throughput, spread clients across many tables (vary `table_id`)
> rather than one hot table, which better reflects production fan-out.

---

## Early measured numbers (2026-07-29)

These are **first-pass, localhost-loopback** figures on a dev box — not a
production sizing. They exist to show the engine is wired correctly and to give
a baseline; re-run on your target hardware and replace them.

**Environment**

| Item | Value |
|------|-------|
| CPU | 12th Gen Intel Core i7-12700H (20 logical cores) |
| Runtime | WSL2, localhost loopback (`ws://localhost:9001`) |
| Build | CMake Release (`-DCMAKE_BUILD_TYPE=Release`) |
| DB | SQLite (default) |
| Rate limiter | *relaxed* for this capacity test (production default is ~3 registrations/min/IP) |
| Auth | JWT HS256, PBKDF2 600k — every connection is authenticated |

**Results**

| Metric | Value |
|--------|-------|
| Concurrent WebSocket connections | **300** (all opened, sustained) |
| WS connection-establish p50 / p95 / p99 | **2.0 / 2.5 / 3.3 ms** |
| Connection rate (sequential register+connect) | 11.5 conn/s |
| Authenticated action round-trip p50 / p95 / p99 (HTTP login, server-side) | 77.9 / 82.6 / 84.5 ms |

**How to read these**

- The **3.3 ms p99 connect latency** is the cost of the authenticated WebSocket
  handshake itself (register → JWT → WS subprotocol upgrade). That is the
  number players feel when joining a table.
- The **~85 ms action round-trip** is a server-side processing measurement
  (auth verification + DB lookup) via the login endpoint; it is DB-bound on
  SQLite and will drop substantially on PostgreSQL / a warm cache.
- The **11.5 conn/s** figure is *sequential registration* — registration is not
  the hot path in production (clients reconnect with a stored token, or
  registration is parallelized). It is reported for honesty, not as a
  throughput ceiling.
- The engine serializes actions *per table* (per-table mutex), so aggregate
  throughput scales by spreading clients across tables, not by hammering one
  table. A single hot table is intentionally the worst case.

**Reproduce**

```bash
# 1. start the server (origin allowlist must include the benchmark origin)
POKER_ALLOWED_ORIGINS="http://localhost:5173" ./build/cli/poker_ws_server 9001

# 2. run the harness (pip install websocket-client first)
python3 scripts/benchmark_connections.py --host localhost:9001 \
    --clients 300 --duration 15 --origin http://localhost:5173
```

> Note: the harness measures connection establishment directly. Per-action
> round-trip on an *active* table (bots dealing hands) is gated on game events
> and will be added as a separate measurement — the action-latency row above is
> the authenticated server-side processing baseline.
