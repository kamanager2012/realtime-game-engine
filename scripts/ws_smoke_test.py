#!/usr/bin/env python3
"""End-to-end WebSocket smoke test for poker-engine join flow."""
from __future__ import annotations
import json, sys, time, urllib.parse, urllib.request
try:
    import websocket
except ImportError:
    print("Install: pip install websocket-client"); sys.exit(1)
BASE = "http://localhost:9001"

def register():
    username = f"smoke{int(time.time())}"
    body = json.dumps({"username": username, "password": "test1234", "display_name": "Smoke"}).encode()
    req = urllib.request.Request(f"{BASE}/api/auth/register", data=body, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=5) as resp:
        data = json.load(resp)
    return data["token"], int(data["player_id"])

def drain(ws, limit=8):
    ws.settimeout(0.3)
    last = None
    for _ in range(limit):
        try:
            msg = json.loads(ws.recv())
            if msg.get("type") == "table_state":
                last = msg
        except Exception:
            break
    return last

def main():
    print("[1/4] Register...")
    token, pid = register()
    print(f"      player_id={pid}")
    url = f"ws://localhost:9001/table?table_id=main"
    print("[2/4] WebSocket connect...")
    # Auth token carried via the WebSocket subprotocol (browser-safe); no
    # longer sent in the URL query string.
    ws = websocket.create_connection(url, subprotocols=[token], timeout=10)
    print("[3/4] Subscribe...")
    ws.send(json.dumps({"type":"subscribe","seq":1,"timestamp":"","payload":{"table_id":"main"}}))
    state = drain(ws)
    if not state:
        print("FAIL: no table_state"); return 1
    players = state["payload"].get("players", [])
    occupied = {p["seat_index"] for p in players if p.get("occupied")}
    seat = next((i for i in range(6) if i not in occupied), None)
    if seat is None:
        print("FAIL: no empty seat"); return 1
    print(f"      empty seat={seat}, occupied={sorted(occupied)}")
    drain(ws, 5)
    print("[4/4] join_table...")
    ws.send(json.dumps({"type":"join_table","seq":2,"timestamp":"","payload":{"table_id":"main","seat_index":seat,"buy_in":200}}))
    saw_joined = False
    for _ in range(30):
        try:
            msg = json.loads(ws.recv())
        except Exception:
            time.sleep(0.1)
            continue
        if msg.get("type") == "player_joined":
            saw_joined = True
        if msg.get("type") == "error":
            print("FAIL:", msg["payload"]); return 1
        if msg.get("type") == "table_state":
            me = [p for p in msg["payload"].get("players", []) if p.get("player_id") == pid and p.get("occupied")]
            if me:
                print(f"      seated at #{me[0]['seat_index'] + 1} (player_joined={saw_joined})")
                ws.close(); print("PASS"); return 0
    ws.close(); print("FAIL: not seated after join"); return 1

if __name__ == "__main__": sys.exit(main())
