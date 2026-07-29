#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND="$ROOT/build/cli/poker_ws_server"
FRONTEND="$ROOT/frontend"
PORT_BACKEND="${POKER_PORT:-9001}"
CFR_MODEL="${POKER_CFR_MODEL_PATH:-$ROOT/data/bot_policy.cfr}"

if [[ ! -x "$BACKEND" ]]; then
  echo "Build first: cd $ROOT && cmake --build build --target poker_ws_server cfr_train_tool"
  exit 1
fi

if [[ ! -f "$CFR_MODEL" ]]; then
  echo "CFR bot model missing; bootstrapping -> $CFR_MODEL"
  "$ROOT/scripts/train_cfr_bot_model.sh" "$CFR_MODEL"
fi
export POKER_CFR_MODEL_PATH="$CFR_MODEL"

if [[ ! -d "$FRONTEND/node_modules" ]]; then
  (cd "$FRONTEND" && npm install)
fi

cleanup() {
  [[ -n "${BACKEND_PID:-}" ]] && kill "$BACKEND_PID" 2>/dev/null || true
  [[ -n "${FRONTEND_PID:-}" ]] && kill "$FRONTEND_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

if ! ss -tlnp 2>/dev/null | grep -q ":${PORT_BACKEND} "; then
  echo "Starting backend :${PORT_BACKEND} (CFR model: $POKER_CFR_MODEL_PATH)..."
  (
    cd "$(dirname "$BACKEND")"
    export POKER_DB_PATH="${POKER_DB_PATH:-$ROOT/data/poker_engine.db}"
    export POKER_POSTGRES_URL="${POKER_POSTGRES_URL:-postgresql://poker:poker@localhost:5432/poker}"
    export POKER_INSTANCE_ID="${POKER_INSTANCE_ID:-poker-engine-dev}"
    export POKER_CFR_MODEL_PATH
    ./poker_ws_server "$PORT_BACKEND"
  ) &
  BACKEND_PID=$!
  sleep 0.8
else
  echo "Backend already on :${PORT_BACKEND} (restart to pick up CFR model changes)"
fi

echo "Starting frontend..."
(cd "$FRONTEND" && npm run dev) &
FRONTEND_PID=$!
echo "Backend: http://localhost:${PORT_BACKEND}"
echo "CFR bot:  POKER_CFR_MODEL_PATH=$POKER_CFR_MODEL_PATH"
echo "Smoke:    python3 $ROOT/scripts/ws_smoke_test.py"
wait
