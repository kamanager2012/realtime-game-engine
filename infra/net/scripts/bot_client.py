#!/usr/bin/env python3
"""
Poker bot test client.
Connects to WebSocket server, authenticates, joins table, auto-plays.

Usage:
    python bot_client.py --player-id 1001 --table 1 --host localhost --port 8443
"""

import asyncio
import websockets
import json
import argparse
import random
import logging
import sys

logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)-8s %(message)s',
    datefmt='%H:%M:%S'
)
logger = logging.getLogger(f"Bot")

# Protocol op codes
OP_AUTH = 1
OP_JOIN_TABLE = 10
OP_LEAVE_TABLE = 11
OP_PLAYER_ACTION = 20
OP_CHAT = 30
OP_PING = 100
OP_AUTH_RESULT = 101
OP_TABLE_STATE = 201
OP_ACTION_REQUEST = 202
OP_ACTION_RESULT = 203
OP_HAND_START = 210
OP_HAND_END = 211
OP_PLAYER_JOINED = 220
OP_PLAYER_LEFT = 221
OP_PONG = 400
OP_ERROR = 500


class PokerBot:
    def __init__(self, player_id: int, table_id: int, host: str, port: int):
        self.player_id = player_id
        self.table_id = table_id
        self.host = host
        self.port = port
        self.ws = None
        self.chips = 10000
        self.hands_played = 0
        self.hands_won = 0
        self.total_profit = 0

    async def connect(self) -> bool:
        uri = f"ws://{self.host}:{self.port}/ws"
        # Carry the auth token via the WebSocket subprotocol (browser-safe);
        # it is no longer placed in the URL query string.
        try:
            self.ws = await websockets.connect(
                uri, ping_interval=20, ping_timeout=10, subprotocols=[str(self.player_id)]
            )
            logger.info(f"Connected to {uri}")
            return True
        except Exception as e:
            logger.error(f"Connection failed: {e}")
            return False

    async def send(self, msg: dict):
        if self.ws:
            await self.ws.send(json.dumps(msg))

    async def auth(self) -> bool:
        msg = {"op": OP_AUTH, "data": {"token": str(self.player_id)}}
        await self.send(msg)
        try:
            response = await asyncio.wait_for(self.ws.recv(), timeout=5.0)
            data = json.loads(response)
            if data.get("op") == OP_AUTH_RESULT and data["data"].get("success"):
                logger.info(f"Authenticated as player {self.player_id}")
                return True
            else:
                logger.error(f"Auth failed: {data}")
        except asyncio.TimeoutError:
            logger.error("Auth timeout")
        return False

    async def recv(self) -> str:
        return await asyncio.wait_for(self.ws.recv(), timeout=60.0)

    def decide_action(self, action_request: dict) -> tuple:
        pot = action_request.get("pot", 0)
        current_bet = action_request.get("current_bet", 0)
        rng = random.random()
        if current_bet == 0:
            if rng < 0.3:
                return ("bet", min(self.chips, pot // 2 if pot > 0 else 100))
            else:
                return ("check", 0)
        else:
            to_call = current_bet
            if rng < 0.25:
                return ("raise", min(int(to_call * 1.5), self.chips))
            elif rng < 0.75:
                return ("call", min(to_call, self.chips))
            else:
                return ("fold", 0)

    async def handle_message(self, raw: str):
        data = json.loads(raw)
        op = data.get("op", 0)
        msg_data = data.get("data", {})

        if op == OP_PING:
            await self.send({"op": OP_PING, "ts": msg_data.get("ts", 0)})
        elif op == OP_TABLE_STATE:
            logger.debug(f"State: pot={msg_data.get('pot',0)}")
        elif op == OP_ACTION_REQUEST:
            self.hands_played += 1
            action, amount = self.decide_action(msg_data)
            logger.info(f"Action: {action} {amount}")
            await self.send({"op": OP_PLAYER_ACTION, "data": {"action": action, "amount": amount}})
        elif op == OP_HAND_START:
            logger.info(f"--- Hand started ---")
        elif op == OP_HAND_END:
            winners = msg_data.get("winners", [])
            pot = msg_data.get("total_pot", 0)
            if self.player_id in winners:
                self.hands_won += 1
                self.total_profit += pot // max(len(winners), 1)
            logger.info(f"--- Hand ended: winners={winners}, pot={pot} ---")
        elif op == OP_AUTH_RESULT:
            pass  # handled in auth()
        elif op == OP_ERROR:
            logger.error(f"Server error: {msg_data}")

    async def run(self):
        if not await self.connect():
            return
        if not await self.auth():
            logger.error("Authentication failed")
            return
        logger.info("Bot running...")
        try:
            while True:
                raw = await self.recv()
                await self.handle_message(raw)
        except websockets.exceptions.ConnectionClosed:
            logger.warning("Connection closed")
        except asyncio.TimeoutError:
            logger.warning("Receive timeout")
        except KeyboardInterrupt:
            pass
        finally:
            logger.info(f"Stats: played={self.hands_played} won={self.hands_won} profit={self.total_profit}")


async def main():
    parser = argparse.ArgumentParser(description="Poker Bot Client")
    parser.add_argument("--player-id", type=int, default=1001)
    parser.add_argument("--table", type=int, default=1)
    parser.add_argument("--host", type=str, default="localhost")
    parser.add_argument("--port", type=int, default=8443)
    args = parser.parse_args()

    bot = PokerBot(args.player_id, args.table, args.host, args.port)
    await bot.run()


if __name__ == "__main__":
    asyncio.run(main())
