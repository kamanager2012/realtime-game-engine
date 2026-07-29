#!/bin/bash
# Start multiple bots for testing
HOST=${1:-localhost}
PORT=${2:-8443}
NUM_BOTS=${3:-4}
BASE_PLAYER_ID=2000

echo "Starting $NUM_BOTS bots against $HOST:$PORT"
echo "Player IDs: $BASE_PLAYER_ID to $((BASE_PLAYER_ID + NUM_BOTS - 1))"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

for i in $(seq 0 $((NUM_BOTS - 1))); do
    player_id=$((BASE_PLAYER_ID + i))
    python3 "$SCRIPT_DIR/bot_client.py" --player-id $player_id --table 1 \
        --host $HOST --port $PORT &
    echo "Started bot: player_id=$player_id (PID: $!)"
    sleep 0.5
done

echo ""
echo "All $NUM_BOTS bots started. Press Ctrl+C to stop."
wait
