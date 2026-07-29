import { useEffect, useCallback, useRef } from 'react';
import { useGameStore } from '../store/gameStore';
import { useWebSocket } from './useWebSocket';
import { ActionType, GamePhase } from '../types/game';

export function useGameEngine() {
  const gameState = useGameStore((s) => s.gameState);
  const playerId = useGameStore((s) => s.playerId);
  const { sendAction } = useWebSocket();

  const isMyTurn = playerId > 0 && !!gameState && gameState.current_player_id === playerId;
  const myPlayer = playerId > 0 ? gameState?.players.find((p) => p.player_id === playerId) : undefined;

  const getAvailableActions = useCallback(() => {
    if (!gameState || !myPlayer) {
      return { canFold: false, canCheck: false, canCall: false, canBet: false, canRaise: false, callAmount: 0, minRaise: 0 };
    }

    const betDiff = gameState.current_bet - myPlayer.bet_this_round;
    const canCheck = betDiff === 0;
    const canCall = betDiff > 0;
    const canBet = betDiff === 0 && gameState.current_bet === 0;

    return {
      canFold: true,
      canCheck,
      canCall,
      canBet,
      canRaise: (canCall || canBet) && gameState.min_raise_size > 0,
      callAmount: Math.min(betDiff, myPlayer.chips),
      minRaise: gameState.min_raise_size || gameState.big_blind * 2,
    };
  }, [gameState, myPlayer]);

  const handleAction = useCallback((action: ActionType, amount: number = 0) => {
    sendAction(action, amount);
  }, [sendAction]);

  return {
    gameState,
    isMyTurn,
    myPlayer,
    getAvailableActions,
    handleAction,
  };
}
