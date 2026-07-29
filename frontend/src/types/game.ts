export enum GamePhase {
  Waiting = "waiting",
  Preflop = "preflop",
  Flop = "flop",
  Turn = "turn",
  River = "river",
  Showdown = "showdown",
  HandOver = "handOver",
}

export enum ActionType {
  Fold = "fold",
  Check = "check",
  Call = "call",
  Bet = "bet",
  Raise = "raise",
  AllIn = "all_in",
}

export enum PlayerStatus {
  Active = "active",
  Folded = "folded",
  AllIn = "all_in",
  SittingOut = "sitting_out",
  Disconnected = "disconnected",
}

export interface Card {
  suit: number; // 0-3
  rank: number; // 0-12
}

export interface PlayerState {
  player_id: number;
  seat_index: number;
  chips: number;
  bet_this_round: number;
  total_invested: number;
  hole_cards: number[];
  status: PlayerStatus;
  action_status: string;
  occupied: boolean;
  display_name?: string;
}

export interface Pot {
  amount: number;
  eligibility_cap: number;
  eligible_player_ids: number[];
}

export interface GameState {
  table_id: string;
  hand_number: number;
  phase: GamePhase;
  status: "idle" | "playing" | "paused";
  dealer_seat: number;
  community_cards: number[];
  current_player_id: number;
  current_bet: number;
  min_raise_size: number;
  pot: number;
  side_pots: Pot[];
  action_history: ActionRecord[];
  winners: number[];
  players: PlayerState[];
  big_blind: number;
}

export interface ActionRecord {
  player_id: number;
  action: ActionType;
  amount: number;
  street: GamePhase;
}

export interface TableConfig {
  table_id: string;
  max_players: number;
  small_blind: number;
  big_blind: number;
  min_buy_in: number;
  max_buy_in: number;
  ante: number;
}

export interface PlayerStats {
  player_id: number;
  name: string;
  display_name: string;
  hands_seen: number;
  hands_vpip: number;
  vpip_pct: number;
  hands_pfr: number;
  pfr_pct: number;
  bets: number;
  calls: number;
  af: number;
  hands_won: number;
  win_rate: number;
  total_net: number;
  avg_bb_per_100: number;
}
