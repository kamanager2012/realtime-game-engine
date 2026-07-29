import React, { useCallback, useEffect, useState } from 'react';
import { useGameStore } from '../store/gameStore';
import { getApiBaseUrl } from '../utils/network';

type GameType = 'nlhe' | 'plo';

const BUY_IN_OPTIONS = [50, 100, 200, 500, 1000];

const MatchmakingPanel: React.FC = () => {
  const playerId = useGameStore((s) => s.playerId);
  const playerName = useGameStore((s) => s.playerName);
  const [gameType, setGameType] = useState<GameType>('nlhe');
  const [buyIn, setBuyIn] = useState(200);
  const [inQueue, setInQueue] = useState(false);
  const [position, setPosition] = useState(0);
  const [matchResult, setMatchResult] = useState<string | null>(null);
  const [joining, setJoining] = useState(false);

  // Poll status when in queue
  useEffect(() => {
    if (!inQueue || playerId <= 0) return;
    const poll = async () => {
      try {
        const res = await fetch(`${getApiBaseUrl()}/api/matchmaking/status?player_id=${playerId}`);
        if (res.ok) {
          const data = await res.json();
          if (!data.in_queue) {
            setInQueue(false);
            setMatchResult('已离开匹配队列');
          } else {
            setPosition(data.position || 0);
          }
        }
      } catch {}
    };
    poll();
    const t = setInterval(poll, 3000);
    return () => clearInterval(t);
  }, [inQueue, playerId]);

  const joinQueue = useCallback(async () => {
    if (playerId <= 0) return;
    setJoining(true);
    setMatchResult(null);
    try {
      const res = await fetch(`${getApiBaseUrl()}/api/matchmaking/join`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          player_id: playerId,
          name: playerName,
          game_type: gameType,
          buy_in: buyIn,
        }),
      });
      const data = await res.json();
      if (data.result === 'matched') {
        setMatchResult(`匹配成功！牌桌: ${data.table_id}`);
        setInQueue(false);
      } else if (data.result === 'waiting') {
        setInQueue(true);
        setPosition(data.position || 0);
      } else if (data.result === 'already_in_queue') {
        setInQueue(true);
      } else {
        setMatchResult('加入失败，请重试');
      }
    } catch {
      setMatchResult('网络错误');
    }
    setJoining(false);
  }, [playerId, playerName, gameType, buyIn]);

  const leaveQueue = useCallback(async () => {
    try {
      await fetch(`${getApiBaseUrl()}/api/matchmaking/leave`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ player_id: playerId }),
      });
    } catch {}
    setInQueue(false);
    setMatchResult(null);
  }, [playerId]);

  return (
    <div className="matchmaking-panel">
      <h3>⚔️ 快速匹配</h3>

      {!inQueue ? (
        <div className="mm-setup">
          <div className="mm-option">
            <label>游戏类型</label>
            <div className="mm-game-type">
              <button
                className={`mm-type-btn ${gameType === 'nlhe' ? 'active' : ''}`}
                onClick={() => setGameType('nlhe')}
              >
                NLHE
              </button>
              <button
                className={`mm-type-btn ${gameType === 'plo' ? 'active' : ''}`}
                onClick={() => setGameType('plo')}
              >
                PLO
              </button>
            </div>
          </div>
          <div className="mm-option">
            <label>买入</label>
            <div className="mm-buyin">
              {BUY_IN_OPTIONS.map((b) => (
                <button
                  key={b}
                  className={`mm-buyin-btn ${buyIn === b ? 'active' : ''}`}
                  onClick={() => setBuyIn(b)}
                >
                  ${b}
                </button>
              ))}
            </div>
          </div>
          <button className="btn-join-mm" onClick={joinQueue} disabled={joining || playerId <= 0}>
            {joining ? '匹配中...' : '开始匹配'}
          </button>
          {matchResult && (
            <div className="mm-result">{matchResult}</div>
          )}
        </div>
      ) : (
        <div className="mm-waiting">
          <div className="mm-spinner" />
          <p className="mm-searching">正在寻找对手...</p>
          <p className="mm-info">
            {gameType === 'plo' ? 'PLO' : 'NLHE'} · ${buyIn} 买入 · 队列位置: {position}
          </p>
          <button className="btn-leave-mm" onClick={leaveQueue}>
            取消匹配
          </button>
        </div>
      )}
    </div>
  );
};

export default MatchmakingPanel;
