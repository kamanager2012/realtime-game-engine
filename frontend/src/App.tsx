import React, { useEffect, useMemo, useCallback } from 'react';
import { useGameStore } from './store/gameStore';
import { useWebSocket } from './hooks/useWebSocket';
import { MessageType } from './types/ws';
import { ActionType } from './types/game';
import { hasDealtHoleCards } from './utils/cards';
import GameTable from './components/GameTable';
import ActionPanel from './components/ActionPanel';
import Lobby from './components/Lobby';
import AuthModal from './components/AuthModal';
import GameHUD from './components/GameHUD';
import SeatPicker from './components/SeatPicker';
import ChatPanel from './components/ChatPanel';
import DailyBonus from './components/DailyBonus';
import PlayerInfoModal from './components/PlayerInfoModal';
import ToastContainer from './components/Toast';

const App: React.FC = () => {
  const token = useGameStore((s) => s.token);
  const playerId = useGameStore((s) => s.playerId);
  const playerName = useGameStore((s) => s.playerName);
  const currentTableId = useGameStore((s) => s.currentTableId);
  const gameState = useGameStore((s) => s.gameState);
  const showAuthModal = useGameStore((s) => s.showAuthModal);
  const setShowAuthModal = useGameStore((s) => s.setShowAuthModal);
  const showLobby = useGameStore((s) => s.showLobby);
  const setShowLobby = useGameStore((s) => s.setShowLobby);
  const setAuth = useGameStore((s) => s.setAuth);
  const clearAuth = useGameStore((s) => s.clearAuth);
  const setPlayerInfoTarget = useGameStore((s) => s.setPlayerInfoTarget);
  const joinTableStore = useGameStore((s) => s.joinTable);
  const leaveTableStore = useGameStore((s) => s.leaveTable);
  const refreshAccountChips = useGameStore((s) => s.refreshAccountChips);
  const addToast = useGameStore((s) => s.addToast);
  const isSeating = useGameStore((s) => s.isSeating);
  const pendingSeatIndex = useGameStore((s) => s.pendingSeatIndex);
  const setPendingSeat = useGameStore((s) => s.setPendingSeat);

  const { connect, sendAction, sendMessage, isConnected, disconnect, requestLeaveTable } = useWebSocket();

  const handleAuth = useCallback(
    (token: string, playerId: number, playerName: string) => {
      setAuth(token, playerId, playerName);
      setShowAuthModal(false);
    },
    [setAuth, setShowAuthModal]
  );

  const handleLeaveTable = useCallback(() => {
    requestLeaveTable();
  }, [requestLeaveTable]);

  useEffect(() => {
    if (token) void refreshAccountChips();
  }, [token, refreshAccountChips]);

  const handleLogout = useCallback(() => {
    if (currentTableId) {
      sendMessage(MessageType.LeaveTable, { table_id: currentTableId });
      leaveTableStore();
      disconnect({ suppressReconnect: true });
    } else {
      leaveTableStore();
      disconnect({ suppressReconnect: true });
    }
    clearAuth();
  }, [currentTableId, clearAuth, sendMessage, leaveTableStore, disconnect]);

  useEffect(() => { if (!token) setShowAuthModal(true); }, [token, setShowAuthModal]);

  const handleJoinTable = useCallback(
    (tableId: string) => {
      if (!token) { setShowAuthModal(true); return; }
      joinTableStore(tableId);
      setShowLobby(false);
      connect(tableId);
    },
    [token, connect, joinTableStore, setShowLobby, setShowAuthModal]
  );

  const handleJoinWithBots = useCallback(
    (tableId: string, botCount: number) => {
      if (!token) { setShowAuthModal(true); return; }
      joinTableStore(tableId);
      setShowLobby(false);
      connect(tableId, botCount);
    },
    [token, connect, joinTableStore, setShowLobby, setShowAuthModal]
  );

  const handleStartGame = useCallback(() => {
    if (!currentTableId) return;
    sendMessage(MessageType.StartGame, { table_id: currentTableId });
  }, [currentTableId, sendMessage]);

  const handleSendAction = useCallback(
    (action: ActionType, amount: number) => { sendAction(action, amount); },
    [sendAction]
  );

  const myPlayerState = useMemo(() => {
    if (!gameState || playerId <= 0) return undefined;
    return gameState.players.find((p) => p.player_id === playerId);
  }, [gameState, playerId]);

  const canSelectSeat = useMemo(
    () => !!gameState && !myPlayerState?.occupied && !isSeating,
    [gameState, myPlayerState, isSeating]
  );

  const handleSeatClick = useCallback(
    (seatIndex: number) => {
      if (!currentTableId || !playerName || !gameState) return;
      if (myPlayerState?.occupied) {
        addToast('info', '你已经在座位上了');
        return;
      }
      if (isSeating) {
        addToast('info', '正在入座，请稍候');
        return;
      }
      const seat = gameState.players[seatIndex];
      if (!seat || seat.occupied) {
        addToast('error', `座位 #${seatIndex + 1} 已被占用`);
        return;
      }
      setPendingSeat(seatIndex);
      const ok = sendMessage(MessageType.JoinTable, {
        table_id: currentTableId,
        player_name: playerName,
        seat_index: seatIndex,
        buy_in: 200,
      });
      if (!ok) {
        setPendingSeat(null);
        addToast('error', '连接断开，无法入座');
        return;
      }
      addToast('info', `正在入座 #${seatIndex + 1}...`);
    },
    [currentTableId, playerName, gameState, myPlayerState, isSeating, sendMessage, addToast, setPendingSeat]
  );

  const isMyTurn = useMemo(
    () => playerId > 0 && !!gameState && !!myPlayerState && gameState.current_player_id === playerId,
    [gameState, myPlayerState, playerId]
  );

  const actions = useMemo(() => {
    if (!gameState || !myPlayerState)
      return { canFold: false, canCheck: false, canCall: false, canBet: false, canRaise: false, callAmount: 0, minRaise: 0 };

    const betDiff = gameState.current_bet - myPlayerState.bet_this_round;
    const canCheck = betDiff === 0;
    const canCall = betDiff > 0;
    const canBet = betDiff === 0;

    return {
      canFold: true,
      canCheck,
      canCall,
      canBet,
      canRaise: canBet || canCall,
      callAmount: Math.min(betDiff, myPlayerState.chips),
      minRaise: gameState.min_raise_size || gameState.big_blind * 2,
    };
  }, [gameState, myPlayerState]);

  const occupiedCount = useMemo(
    () => (gameState ? gameState.players.filter((p) => p.occupied).length : 0),
    [gameState]
  );

  const inCurrentHand = hasDealtHoleCards(myPlayerState?.hole_cards);
  const waitingNextHand = !!myPlayerState?.occupied && gameState?.status === 'playing' && !inCurrentHand;
  const actionEpoch = gameState
    ? `${gameState.phase}-${gameState.current_player_id}-${gameState.current_bet}-${myPlayerState?.bet_this_round ?? 0}`
    : '';

  const canStartGame = useMemo(
    () =>
      !!gameState &&
      gameState.status === 'idle' &&
      !!myPlayerState?.occupied &&
      occupiedCount >= 2,
    [gameState, myPlayerState, occupiedCount]
  );

  const inGame = !showLobby;
  const showGameUI = inGame && !!gameState;
  const showConnecting = inGame && !gameState;

  return (
    <div className="poker-app">
      {showAuthModal && <AuthModal onAuthenticated={handleAuth} />}
      {showGameUI && <GameHUD onLeaveTable={handleLeaveTable} />}

      {showConnecting && (
        <div style={{ textAlign: 'center', padding: '80px 20px', color: '#ffd700' }}>
          <p style={{ fontSize: 22 }}>正在加载牌桌...</p>
          <p style={{ opacity: 0.7, marginTop: 8 }}>连接成功后请点击空位入座</p>
        </div>
      )}

      {showLobby && (
        <Lobby
          onJoinTable={handleJoinTable}
          onJoinWithBots={handleJoinWithBots}
          onLogout={handleLogout}
          playerName={playerName}
        />
      )}

      {showGameUI && (
        <div className="game-area">
          <div className="game-table-area">
            {canSelectSeat && (
              <SeatPicker
                players={gameState.players}
                pendingSeatIndex={pendingSeatIndex}
                isSeating={isSeating}
                onSelectSeat={handleSeatClick}
              />
            )}
            <GameTable
              gameState={gameState}
              myPlayerId={playerId}
              mySeatIndex={myPlayerState?.seat_index ?? null}
              isMyTurn={isMyTurn}
              onAction={handleSendAction}
              onSeatClick={handleSeatClick}
              canSelectSeat={canSelectSeat}
            />
            {waitingNextHand && (
              <p style={{ textAlign: 'center', color: '#8ec8ff', marginTop: 12, fontSize: 14 }}>
                本手牌进行中，你已入座，请等待下一手开始（发牌后 HUD 会显示你的手牌）
              </p>
            )}
            {gameState.status === 'idle' && (
              <div style={{ textAlign: 'center', marginTop: 12 }}>
                {!myPlayerState?.occupied ? (
                  <p style={{ color: '#aaa', fontSize: 14 }}>请先点击空位入座，再开始游戏</p>
                ) : occupiedCount < 2 ? (
                  <p style={{ color: '#aaa', fontSize: 14 }}>至少需要 2 名玩家才能开始（当前 {occupiedCount} 人）</p>
                ) : (
                  <button
                    onClick={handleStartGame}
                    disabled={!canStartGame}
                    style={{
                      fontSize: 20, padding: '12px 40px', background: 'linear-gradient(135deg, #ff6b35, #f7c948)',
                      border: 'none', borderRadius: 12, color: '#1a1a2e', fontWeight: 'bold', cursor: 'pointer',
                      boxShadow: '0 4px 15px rgba(255,107,53,0.4)', opacity: canStartGame ? 1 : 0.5,
                    }}
                  >
                    🃏 开始游戏
                  </button>
                )}
              </div>
            )}
          </div>
          <aside className="side-panel">
            <h3>👥 玩家 ({gameState.players.filter((p) => p.occupied).length}/6)</h3>
            <div className="player-list">
              {gameState.players.filter((p) => p.occupied).map((p) => (
                <div
                  key={p.player_id}
                  className={['player-list-item', playerId > 0 && p.player_id === playerId ? 'self' : '', p.status].filter(Boolean).join(' ')}
                  onClick={() => setPlayerInfoTarget(p)}
                  role="button"
                  tabIndex={0}
                >
                  <span className="player-list-name">
                    {p.display_name || `玩家 ${p.player_id}`}{playerId > 0 && p.player_id === playerId && ' (你)'}
                  </span>
                  <span className="player-list-chips">💰 {p.chips}</span>
                  <span className={`status-dot-sm status-${p.status}`} />
                </div>
              ))}
            </div>
            <ChatPanel />
          </aside>
          <DailyBonus />
        </div>
      )}

      {showGameUI && isMyTurn && myPlayerState && inCurrentHand && (
        <div className="action-panel-wrapper">
          <ActionPanel
            onSendAction={handleSendAction}
            myChips={myPlayerState.chips}
            currentBet={gameState!.current_bet}
            myBet={myPlayerState.bet_this_round}
            minRaise={actions.minRaise}
            canCheck={actions.canCheck}
            canCall={actions.canCall}
            callAmount={actions.callAmount}
            minBet={actions.minRaise}
            maxBet={myPlayerState.chips}
            actionEpoch={actionEpoch}
          />
        </div>
      )}

      <ToastContainer />
      <PlayerInfoModal />

      <div className={`connection-indicator ${isConnected ? 'connected' : 'disconnected'}`}>
        {isConnected ? '● 在线' : '○ 断线'}
      </div>
    </div>
  );
};

export default App;
