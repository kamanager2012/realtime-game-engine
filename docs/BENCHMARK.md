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
