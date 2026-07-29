import { create } from 'zustand';
import { fetchAccountChips } from '../utils/network';
import { GameState, PlayerState, ActionType } from '../types/game';

export interface Toast {
  id: string;
  type: 'success' | 'error' | 'info';
  message: string;
  timestamp: number;
}

interface GameStore {
  // === 认证 ===
  token: string | null;
  playerId: number;
  playerName: string;
  accountChips: number | null;
  setAccountChips: (chips: number | null) => void;
  setAuth: (token: string, playerId: number, playerName: string) => void;
  refreshAccountChips: () => Promise<void>;
  clearAuth: () => void;

  // === 大厅 ===
  tables: Map<string, { name: string; playerCount: number; maxPlayers: number }>;
  setTables: (tables: Map<string, { name: string; playerCount: number; maxPlayers: number }>) => void;

  // === 牌桌 ===
  currentTableId: string | null;
  gameState: GameState | null;
  setGameState: (state: GameState) => void;
  joinTable: (tableId: string, seat?: number, buyIn?: number) => void;
  leaveTable: () => void;
  pendingSeatIndex: number | null;
  isSeating: boolean;
  setPendingSeat: (seat: number | null) => void;
  clearPendingSeat: () => void;

  // === UI 状态 ===
  showAuthModal: boolean;
  setShowAuthModal: (show: boolean) => void;
  showLobby: boolean;
  setShowLobby: (show: boolean) => void;
  chatMessages: Array<{ player: string; message: string; timestamp: string }>;
  addChatMessage: (msg: { player: string; message: string; timestamp: string }) => void;

  // === 行动选择 ===
  selectedAction: { type: ActionType; amount: number } | null;
  setSelectedAction: (action: { type: ActionType; amount: number } | null) => void;

  // === 玩家信息弹窗 ===
  playerInfoTarget: PlayerState | null;
  setPlayerInfoTarget: (player: PlayerState | null) => void;

  // === Toast ===
  toasts: Toast[];
  addToast: (type: Toast['type'], message: string) => void;
  removeToast: (id: string) => void;
}

function loadStoredAuth() {
  const token = localStorage.getItem('poker_token');
  const playerId = Number(localStorage.getItem('poker_player_id') ?? '-1');
  const playerName = localStorage.getItem('poker_player_name') ?? '';
  if (!token || !Number.isFinite(playerId) || playerId <= 0 || !playerName) {
    localStorage.removeItem('poker_token');
    localStorage.removeItem('poker_player_id');
    localStorage.removeItem('poker_player_name');
    return { token: null, playerId: -1, playerName: '', accountChips: null };
  }
  return { token, playerId, playerName, accountChips: null };
}

const storedAuth = loadStoredAuth();

export const useGameStore = create<GameStore>((set, get) => ({
  token: storedAuth.token,
  playerId: storedAuth.playerId,
  playerName: storedAuth.playerName,
  accountChips: null,
  setAuth: (token, playerId, playerName) => {
    localStorage.setItem('poker_token', token);
    localStorage.setItem('poker_player_id', String(playerId));
    localStorage.setItem('poker_player_name', playerName);
    set({ token, playerId, playerName });
  },
  setAccountChips: (chips) => set({ accountChips: chips }),
  refreshAccountChips: async () => {
    const { token } = get();
    if (!token) return;
    const info = await fetchAccountChips(token);
    if (info) set({ accountChips: info.chips });
  },
  clearAuth: () => {
    localStorage.removeItem('poker_token');
    localStorage.removeItem('poker_player_id');
    localStorage.removeItem('poker_player_name');
    set({
      token: null,
      playerId: -1,
      playerName: '',
      accountChips: null,
      currentTableId: null,
      gameState: null,
      showLobby: true,
      showAuthModal: true,
    });
  },

  tables: new Map(),
  setTables: (tables) => set({ tables }),

  currentTableId: null,
  gameState: null,
  setGameState: (state) => set({ gameState: state }),
  joinTable: (tableId) => set({ currentTableId: tableId, gameState: null }),
  pendingSeatIndex: null,
  isSeating: false,
  setPendingSeat: (seat) => set({ pendingSeatIndex: seat, isSeating: seat !== null }),
  clearPendingSeat: () => set({ pendingSeatIndex: null, isSeating: false }),
  leaveTable: () => set({ currentTableId: null, gameState: null, showLobby: true, pendingSeatIndex: null, isSeating: false }),

  showAuthModal: false,
  setShowAuthModal: (show) => set({ showAuthModal: show }),
  showLobby: true,
  setShowLobby: (show) => set({ showLobby: show }),
  chatMessages: [],
  addChatMessage: (msg) =>
    set((state) => ({
      chatMessages: [...state.chatMessages.slice(-99), msg],
    })),

  selectedAction: null,
  setSelectedAction: (action) => set({ selectedAction: action }),

  playerInfoTarget: null,
  setPlayerInfoTarget: (player) => set({ playerInfoTarget: player }),

  toasts: [],
  addToast: (type, message) => {
    const id = Date.now().toString() + Math.random().toString(36).slice(2);
    set((state) => ({
      toasts: [...state.toasts.slice(-4), { id, type, message, timestamp: Date.now() }],
    }));
    setTimeout(() => {
      set((state) => ({
        toasts: state.toasts.filter((t) => t.id !== id),
      }));
    }, 3000);
  },
  removeToast: (id) =>
    set((state) => ({
      toasts: state.toasts.filter((t) => t.id !== id),
    })),
}));
