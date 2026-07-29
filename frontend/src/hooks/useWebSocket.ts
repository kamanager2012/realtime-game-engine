import { useCallback, useState, useRef } from 'react';
import { useGameStore } from '../store/gameStore';
import { WSMessage, MessageType } from '../types/ws';
import { GameState, PlayerState } from '../types/game';
import { getWebSocketBaseUrl } from '../utils/network';

let wsInstance: WebSocket | null = null;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let heartbeatTimer: ReturnType<typeof setTimeout> | null = null;
let messageSeq = 0;
let wsSession = 0; // incremented on each new connection to prevent stale onclose handlers
let suppressReconnect = false;

type MessageHandler = (msg: WSMessage) => void;
const subscribers: Map<string, Set<MessageHandler>> = new Map();

export function subscribe(
  type: string,
  handler: MessageHandler
): () => void {
  if (!subscribers.has(type)) subscribers.set(type, new Set());
  subscribers.get(type)!.add(handler);
  return () => subscribers.get(type)?.delete(handler);
}

function emit(type: string, msg: WSMessage) {
  subscribers.get('*')?.forEach((h) => h(msg));
  subscribers.get(type)?.forEach((h) => h(msg));
}

function emptySeat(seatIdx: number): PlayerState {
  return {
    player_id: -1,
    seat_index: seatIdx,
    chips: 0,
    bet_this_round: 0,
    total_invested: 0,
    hole_cards: [],
    status: 'active' as PlayerState['status'],
    action_status: 'none',
    occupied: false,
  };
}

function normalizePlayers(raw: unknown): PlayerState[] {
  const arr = Array.isArray(raw) ? (raw as PlayerState[]) : [];
  const seats: PlayerState[] = [];
  for (let i = 0; i < 6; i++) {
    const found = arr.find((p) => p.seat_index === i);
    if (found && typeof found === 'object' && found.occupied) {
      seats.push({ ...emptySeat(i), ...found, seat_index: i });
    } else {
      seats.push(emptySeat(i));
    }
  }
  return seats;
}

function transformState(p: Record<string, unknown>): GameState | null {
  if (!p.table_id && (!Array.isArray(p.players) || (p.players as unknown[]).length === 0)) {
    return null;
  }
  return {
    table_id: (p.table_id as string) || '',
    hand_number: (p.hand_number as number) || 0,
    phase: ((p.phase as string) as GameState['phase']) || 'waiting',
    dealer_seat: (p.dealer_seat as number) || 0,
    community_cards: (p.community_cards as number[]) || [],
    current_player_id: (p.current_player_id as number) ?? -1,
    current_bet: (p.current_bet as number) || 0,
    min_raise_size: (p.min_raise as number) || 0,
    pot: (p.pot as number) || 0,
    status: ((p.status as string) as GameState['status']) || 'idle',
    side_pots: (p.side_pots as GameState['side_pots']) || [],
    action_history: [],
    winners: (p.winners as number[]) || [],
    players: normalizePlayers(p.players),
    big_blind: (p.big_blind as number) || 2,
  };
}

export function useWebSocket() {
  const [connected, setConnected] = useState(false);
  const token = useGameStore((s) => s.token);
  const currentTableId = useGameStore((s) => s.currentTableId);
  const setGameState = useGameStore((s) => s.setGameState);
  const addChatMessage = useGameStore((s) => s.addChatMessage);
  const addToast = useGameStore((s) => s.addToast);
  const clearPendingSeat = useGameStore((s) => s.clearPendingSeat);
  const clearAuth = useGameStore((s) => s.clearAuth);

  // Ref to hold latest connect (avoids circular useCallback deps)
  const connectRef = useRef<(tableId: string, botCount?: number) => void>(() => {});
  // Ref for scheduleReconnect itself
  const scheduleReconnectRef = useRef<(tableId: string, botCount?: number) => void>(() => {});

  const stopHeartbeat = useCallback(() => {
    if (heartbeatTimer) { clearInterval(heartbeatTimer); heartbeatTimer = null; }
  }, []);

  const disconnect = useCallback((opts?: { suppressReconnect?: boolean }) => {
    if (opts?.suppressReconnect) suppressReconnect = true;
    stopHeartbeat();
    setConnected(false);
    if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
    if (wsInstance) {
      wsSession++; // prevent the pending onclose from scheduling reconnect
      try { wsInstance.close(); } catch { /* ignore */ }
      wsInstance = null;
    }
  }, [stopHeartbeat]);

  const sendMessage = useCallback(
    (type: MessageType | string, payload: Record<string, unknown> = {}) => {
      if (!wsInstance || wsInstance.readyState !== WebSocket.OPEN) {
        console.warn('[WS] Not connected, dropping:', type);
        return false;
      }
      messageSeq++;
      const msg: WSMessage = {
        type: type as MessageType,
        seq: messageSeq,
        timestamp: new Date().toISOString(),
        payload,
      };
      wsInstance.send(JSON.stringify(msg));
      return true;
    },
    []
  );

  const startHeartbeat = useCallback(() => {
    stopHeartbeat();
    heartbeatTimer = setInterval(() => {
      if (wsInstance?.readyState === WebSocket.OPEN) {
        sendMessage(MessageType.Heartbeat, {});
      }
    }, 30000);
  }, [sendMessage, stopHeartbeat]);

  const scheduleReconnect = useCallback(
    (tableId: string, botCount = 0) => {
      if (reconnectTimer) return;
      let attempts = 0;
      const max = 6;

      const tryReconnect = () => {
        attempts++;
        if (attempts > max) return;
        const delay = Math.min(1000 * Math.pow(2, attempts), 30000);
        reconnectTimer = setTimeout(() => {
          reconnectTimer = null;
          connectRef.current(tableId, botCount);
        }, delay);
      };
      tryReconnect();
    },
    [] // forward ref — connectRef used at call time
  );

  // Keep scheduleReconnectRef updated
  scheduleReconnectRef.current = scheduleReconnect;

  const connect = useCallback(
    (tableId: string, botCount = 0) => {
      suppressReconnect = false;
      disconnect();

      const url = `${getWebSocketBaseUrl()}/table?table_id=${encodeURIComponent(tableId)}`;
      // Carry the auth token as the WebSocket subprotocol (browser-safe).
      // It is intentionally NOT placed in the URL query string, so it cannot
      // leak into proxy/edge access logs or browser history.
      console.log('[WS] Connecting:', url);

      const thisSession = ++wsSession;
      wsInstance = new WebSocket(url, token ? token : undefined);

      wsInstance.onopen = () => {
        console.log('[WS] Connected');
        setConnected(true);
        addToast('success', '已连接到牌桌，请点击空位坐下');
        startHeartbeat();
        sendMessage(MessageType.Subscribe, { table_id: tableId });
        if (botCount > 0) {
          sendMessage(MessageType.AddBots, {
            table_id: tableId,
            count: botCount,
            buy_in: 200,
          });
        }
      };

      wsInstance.onmessage = (event) => {
        try {
          const raw: WSMessage = JSON.parse(event.data);
          const type = raw.type;

          switch (type) {
            case MessageType.TableState: {
              const p = raw.payload as Record<string, unknown>;
              const state = transformState(p);
              if (state) setGameState(state);
              else addToast('error', '无法加载牌桌状态，请返回大厅重试');
              break;
            }
            case MessageType.GameEvent: {
              const p = raw.payload as Record<string, unknown>;
              const eventName = p.event as string;
              if (eventName === 'phase_change') {
                addToast('info', `[阶段] ${p.detail}`);
              } else if (eventName === 'showdown' || eventName === 'hand_over') {
                addToast('success', `[结算] ${p.detail}`);
              }
              break;
            }
            case MessageType.Error: {
              const p = raw.payload as Record<string, unknown>;
              const code = Number(p.code);
              if (code === 400 || code === 409 || code === 402) clearPendingSeat();
              addToast('error', `[错误 ${p.code}] ${p.message}`);
              if (Number(p.code) === 401) {
                clearAuth();
                try { wsInstance?.close(); } catch { /* ignore */ }
              }
              break;
            }
            case 'player_joined': {
              const p = raw.payload as Record<string, unknown>;
              clearPendingSeat();
              const joinBalance = Number(p.balance_after ?? NaN);
              if (Number.isFinite(joinBalance) && joinBalance >= 0) {
                useGameStore.getState().setAccountChips(joinBalance);
              }
              addToast('success', `入座成功：座位 #${(Number(p.seat_index) + 1)}`);
              break;
            }
            case 'player_left': {
              const p = raw.payload as Record<string, unknown>;
              const cashed = Number(p.cashed_out ?? 0);
              const balance = Number(p.balance_after ?? 0);
              const store = useGameStore.getState();
              if (Number.isFinite(balance) && balance >= 0) {
                store.setAccountChips(balance);
              }
              store.leaveTable();
              store.setShowLobby(true);
              suppressReconnect = true;
              stopHeartbeat();
              setConnected(false);
              if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
              if (wsInstance) {
                wsSession++;
                try { wsInstance.close(); } catch { /* ignore */ }
                wsInstance = null;
              }
              void store.refreshAccountChips();
              if (cashed > 0) {
                addToast('success', `已离桌，兑现 ${cashed} 筹码，钱包余额 ${balance}`);
              } else {
                addToast('info', '已离桌');
              }
              break;
            }
            case MessageType.ChatMessage: {
              const p = raw.payload as Record<string, unknown>;
              addChatMessage({
                player: (p.player_name ?? '未知') as string,
                message: (p.message ?? '') as string,
                timestamp: raw.timestamp,
              });
              break;
            }
          }

          emit(type, raw);
          emit('*', raw);
        } catch (err) {
          console.error('[WS] Parse error:', err);
        }
      };

      wsInstance.onclose = (ev) => {
        console.log(`[WS] Closed: code=${ev.code} reason=${ev.reason} wasClean=${ev.wasClean}`);
        if (thisSession !== wsSession) return; // superseded by a newer connection
        setConnected(false);
        stopHeartbeat();
        if (suppressReconnect) {
          suppressReconnect = false;
          return;
        }
        addToast('error', `连接断开(code=${ev.code})，正在重连...`);
        scheduleReconnect(tableId, botCount);
      };

      wsInstance.onerror = (err) => {
        console.error('[WS] Error:', err);
        addToast('error', 'WebSocket 连接出错');
      };
    },
    [token, disconnect, setGameState, addChatMessage, addToast, clearAuth, sendMessage, startHeartbeat, stopHeartbeat, scheduleReconnect]
  );
  // Keep connectRef updated so scheduleReconnect always uses the latest connect
  connectRef.current = connect;

  const sendAction = useCallback(
    (action: string, amount: number = 0) => {
      if (!currentTableId) return false;
      return sendMessage(MessageType.PlayerAction, {
        action,
        amount,
        table_id: currentTableId,
      });
    },
    [currentTableId, sendMessage]
  );

  const sendChat = useCallback(
    (message: string, tableId: string) => {
      return sendMessage(MessageType.ChatMessage, {
        message,
        table_id: tableId,
      });
    },
    [sendMessage]
  );

  const requestLeaveTable = useCallback(() => {
    const tableId = useGameStore.getState().currentTableId;
    if (!tableId) {
      useGameStore.getState().leaveTable();
      useGameStore.getState().setShowLobby(true);
      void useGameStore.getState().refreshAccountChips();
      return false;
    }
    const sent = sendMessage(MessageType.LeaveTable, { table_id: tableId });
    if (!sent) {
      addToast('error', '连接断开，无法离桌');
      return false;
    }
    addToast('info', '正在离桌并兑现筹码...');
    setTimeout(() => {
      const s = useGameStore.getState();
      if (s.currentTableId === tableId) {
        addToast('error', '离桌超时，请稍后在大厅刷新钱包');
        s.leaveTable();
        s.setShowLobby(true);
        suppressReconnect = true;
        disconnect({ suppressReconnect: true });
        void s.refreshAccountChips();
      }
    }, 5000);
    return true;
  }, [sendMessage, addToast, disconnect]);

  return {
    connect,
    disconnect,
    requestLeaveTable,
    sendMessage,
    sendAction,
    sendChat,
    isConnected: connected,
    subscribe,
  };
}
