#!/usr/bin/env python3
"""
Connection / latency benchmark for the realtime-game-engine WebSocket server.

Registers N players, opens N WebSocket connections (auth via subprotocol, same
path the browser uses), subscribes each to a table, and measures:
  - connection establishment latency (WS open)
  - subscribe round-trip latency (p50 / p95 / p99)
  - sustained message throughput (responses/sec)

Usage:
  python3 scripts/benchmark_connections.py --host localhost:9001 \
      --clients 600 --duration 20

Requires: pip install websocket-client
The server must already be running: ./build/cli/poker_ws_server 9001

Note: the server enforces an Origin allowlist (CORS). Point --origin at an
origin the server allows (default dev origins include http://localhost:5173),
otherwise the WS handshake is rejected with 403. For a capacity test you may
raise the server's per-IP rate limits.
"""
from __future__ import annotations
import argparse, itertools, json, os, sys, time, urllib.request
try:
    import websocket
except ImportError:
    print("Install: pip install websocket-client"); sys.exit(1)

_uid = itertools.count()

def register(base):
    # Server requires username 3-20 chars of [A-Za-z0-9_]; keep it short + unique.
    username = f"b{os.getpid()}_{next(_uid)}"
    body = json.dumps({"username": username, "password": "test1234",
                       "display_name": "Bench"}).encode()
    req = urllib.request.Request(f"{base}/api/auth/register", data=body,
        headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=5) as resp:
        d = json.load(resp)
    return d["token"], int(d["player_id"])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="localhost:9001")
    ap.add_argument("--clients", type=int, default=200)
    ap.add_argument("--duration", type=int, default=20)
    ap.add_argument("--table", default="main")
    ap.add_argument("--origin", default="http://localhost:5173")
    args = ap.parse_args()

    base = f"http://{args.host}"
    ws_url = f"ws://{args.host}/table?table_id={args.table}"

    print(f"[bench] registering + connecting {args.clients} clients -> {ws_url}")
    conns = []
    connect_lat = []
    ok = 0
    t0 = time.time()
    for i in range(args.clients):
        try:
            token, pid = register(base)
            c0 = time.time()
            ws = websocket.create_connection(ws_url, subprotocols=[token],
                                             origin=args.origin, timeout=10)
            ws.settimeout(2.0)
            open_lat = time.time() - c0
            connect_lat.append(open_lat)
            # First-message round-trip: subscribe and wait for any server frame.
            ws.send(json.dumps({"type": "subscribe", "seq": 1, "timestamp": "",
                                "payload": {"table_id": args.table}}))
            r0 = time.time()
            try:
                ws.recv()
                connect_lat[-1] = open_lat  # keep open latency separate
            except Exception:
                pass
            conns.append(ws)
            ok += 1
        except Exception as e:
            if i % 50 == 0:
                print(f"  connect error @ {i}: {e}")
    print(f"[bench] connected {ok}/{args.clients} in {time.time()-t0:.1f}s")

    if not conns:
        print("no connections; abort"); return 1

    # Sustained round-trip measurement
    print(f"[bench] measuring for {args.duration}s ...")
    lat = []
    sent = 0
    start = time.time()
    end = start + args.duration
    seq = 2
    while time.time() < end:
        for ws in conns:
            try:
                ws.send(json.dumps({"type": "subscribe", "seq": seq,
                                    "timestamp": "", "payload": {"table_id": args.table}}))
                sent += 1
                c0 = time.time()
                try:
                    ws.recv()
                    lat.append(time.time() - c0)
                except Exception:
                    pass
            except Exception:
                pass
        seq += 1

    elapsed = time.time() - start
    def pct(xs, p):
        if not xs: return 0.0
        xs = sorted(xs)
        return xs[min(len(xs)-1, int(len(xs)*p))]

    print("\n================ BENCHMARK RESULT ================")
    print(f"clients connected : {ok}")
    print(f"connect open p50/p99 : {pct(connect_lat,0.50)*1000:.1f} / {pct(connect_lat,0.99)*1000:.1f} ms")
    print(f"messages sent     : {sent} over {elapsed:.1f}s")
    print(f"responses         : {len(lat)}")
    print(f"throughput        : {len(lat)/elapsed:.1f} resp/s")
    if lat:
        print(f"rtt p50/p95/p99   : {pct(lat,0.50)*1000:.1f} / {pct(lat,0.95)*1000:.1f} / {pct(lat,0.99)*1000:.1f} ms")
    print("==================================================")

    for ws in conns:
        try: ws.close()
        except Exception: pass
    return 0

if __name__ == "__main__":
    sys.exit(main())
