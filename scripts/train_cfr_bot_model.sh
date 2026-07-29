#!/usr/bin/env bash
# Build default CFR bot policy for live poker_ws_server.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TRAIN_BIN="$ROOT/build/cli/cfr_train_tool"
OUTPUT="${1:-$ROOT/data/bot_policy.cfr}"
MODE="${POKER_CFR_TRAIN_MODE:-bootstrap}"
ITERATIONS="${POKER_CFR_ITERATIONS:-500}"

mkdir -p "$(dirname "$OUTPUT")"

if [[ ! -x "$TRAIN_BIN" ]]; then
  echo "[train_cfr] Building cfr_train_tool..."
  cmake --build "$ROOT/build" -j"$(nproc)" --target cfr_train_tool
fi

echo "[train_cfr] mode=$MODE iterations=$ITERATIONS output=$OUTPUT"
"$TRAIN_BIN" --mode "$MODE" --iterations "$ITERATIONS" --output "$OUTPUT"

echo "[train_cfr] Done. Start server with:"
echo "  export POKER_CFR_MODEL_PATH=$OUTPUT"
