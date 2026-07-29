export enum MessageType {
  JoinTable = "join_table",
  LeaveTable = "leave_table",
  PlayerAction = "player_action",
  ChatMessage = "chat_message",
  Subscribe = "subscribe",
  Unsubscribe = "unsubscribe",
  Heartbeat = "heartbeat",
  AddBots = "add_bots",
  StartGame = "start_game",

  TableState = "table_state",
  PlayerJoined = "player_joined",
  PlayerLeft = "player_left",
  ActionTaken = "action_taken",
  GameEvent = "game_event",
  HandResult = "hand_result",
  Error = "error",
  HeartbeatAck = "heartbeat_ack",
}

export interface WSMessage {
  type: string;
  seq: number;
  timestamp: string;
  payload: Record<string, unknown>;
}

// 客户端 → 服务端
export interface JoinTablePayload {
  table_id: string;
  player_name: string;
  seat_index?: number;
  buy_in?: number;
}

export interface PlayerActionPayload {
  table_id: string;
  action: string;
  amount?: number;
}

export interface ChatPayload {
  table_id: string;
  message: string;
}

// 服务端 → 客户端
export interface TableStatePayload {
  table_id: string;
  hand_number: number;
  phase: string;
  dealer_seat: number;
  community_cards: string;
  pot: number;
  current_bet: number;
  min_raise: number;
  current_player_id: number;
  status: string;
  players: PlayerState[];
  action_history: string;
}

export interface ActionTakenPayload {
  player: string;
  action: string;
  amount: number;
  chips_remaining: number;
}

export interface GameEventPayload {
  event: string;
  detail: string;
}

export interface HandResultPayload {
  winners: Array<{
    player: number;
    hand_type: string;
    won_amount: number;
  }>;
}

export interface ErrorPayload {
  code: number;
  message: string;
  ref_seq?: number;
}

interface PlayerState {
  player_id: number;
  seat_index: number;
  chips: number;
  bet_this_round: number;
  total_invested: number;
  hole_cards: number[];
  status: string;
  action_status: string;
  occupied: boolean;
  display_name?: string;
}
