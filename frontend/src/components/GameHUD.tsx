import React, { useState, useEffect } from 'react';
import { cardToString, hasDealtHoleCards } from '../utils/cards';
import { useGameStore } from '../store/gameStore';
import { PlayerStats } from '../types/game';

const PHASE_LABELS: Record<string, string> = {
  waiting: '等待开始',
  preflop: '翻前',
  flop: '翻牌圈',
  turn: '转牌圈',
  river: '河牌圈',
  showdown: '摊牌',
  handOver: '本手牌结束',
};

const statBadgeColor = (vpip: number, pfr: number, af: number) => {
  // Loose-passive (high vpip, low af) = orange
  // Loose-aggressive (high vpip, high af) = red
  // Tight-passive (low vpip, low af) = blue
  // Tight-aggressive (low vpip, high af) = green (TAG = ideal)
  if (vpip > 30 && af > 2) return '#e74c3c'; // LAG — red
  if (vpip > 30) return '#e67e22'; // LP — orange
  if (af > 2) return '#27ae60'; // TAG — green
  return '#3498db'; // TP — blue
};

interface GameHUDProps {
  onLeaveTable: () => void;
}

const GameHUD: React.FC<GameHUDProps> = ({ onLeaveTable }) => {
  const gameState = useGameStore((s) => s.gameState);
  const playerName = useGameStore((s) => s.playerName);
  const playerId = useGameStore((s) => s.playerId);
  const accountChips = useGameStore((s) => s.accountChips);
  const [statsMap, setStatsMap] = useState<Record<number, PlayerStats>>({});

  // Fetch stats for all seated players when table state changes
  useEffect(() => {
    if (!gameState?.players?.length) return;
    const pids = gameState.players
      .filter((p) => p.occupied && p.player_id > 0)
      .map((p) => p.player_id);
    if (pids.length === 0) return;

    Promise.all(
      pids.map((pid) =>
        fetch(`/api/players/${pid}/stats`)
          .then((r) => (r.ok ? r.json() : null))
          .catch(() => null)
      )
    ).then((results) => {
      const map: Record<number, PlayerStats> = {};
      results.forEach((s) => {
        if (s && s.player_id) map[s.player_id] = s;
      });
      setStatsMap(map);
    });
  }, [gameState?.players?.map((p) => p.player_id).join(',')]);

  if (!gameState) return null;

  const myPlayer =
    playerId > 0 ? gameState.players.find((p) => p.player_id === playerId) : undefined;
  const isMyTurn = playerId > 0 && gameState.current_player_id === playerId;

  const myHole = myPlayer?.hole_cards ?? [];
  const inCurrentHand = hasDealtHoleCards(myHole);
  const waitingNextHand =
    !!myPlayer?.occupied && gameState.status === 'playing' && !inCurrentHand;

  return (
    <div className="game-hud">
      <div className="hud-top-bar">
        <div className="hud-player-info">
          <span className="hud-username">{playerName}</span>
          <span className="hud-chips">桌上 💰 {myPlayer?.chips ?? 0}</span>
          {accountChips != null && (
            <span className="hud-wallet">钱包 💰 {accountChips}</span>
          )}
          {statsMap[playerId] && statsMap[playerId].hands_seen > 0 && (
            <span className="hud-my-stats">
              VPIP {statsMap[playerId].vpip_pct.toFixed(0)}% · PFR{' '}
              {statsMap[playerId].pfr_pct.toFixed(0)}% · AF {statsMap[playerId].af.toFixed(1)}
            </span>
          )}
        </div>
        <div className="hud-game-info">
          <span className="hud-phase">{PHASE_LABELS[gameState.phase] || gameState.phase}</span>
          <span className="hud-hand">第 {gameState.hand_number} 手</span>
          <span className="hud-pot">💰 底池: {Math.round(gameState.pot)}</span>
          {inCurrentHand && (
            <span className="hud-hole">
              🃏 {cardToString(myHole[0])} {cardToString(myHole[1])}
            </span>
          )}
        </div>
        <button className="btn-leave" onClick={onLeaveTable}>离开并兑现</button>
      </div>

      {/* Opponent stat badges */}
      <div className="hud-stats-strip">
        {gameState.players
          .filter((p) => p.occupied && p.player_id > 0 && p.player_id !== playerId)
          .map((p) => {
            const s = statsMap[p.player_id];
            if (!s || s.hands_seen < 1) return null;
            const color = statBadgeColor(s.vpip_pct, s.pfr_pct, s.af);
            const label =
              s.vpip_pct > 30 && s.af > 2
                ? 'LAG'
                : s.vpip_pct > 30
                  ? 'LP'
                  : s.af > 2
                    ? 'TAG'
                    : 'TP';
            return (
              <div
                key={p.player_id}
                className="stat-badge"
                style={{ borderColor: color }}
              >
                <span className="badge-name">{p.display_name || `P${p.player_id}`}</span>
                <span className="badge-type" style={{ color }}>{label}</span>
                <span className="badge-stats">
                  V{s.vpip_pct.toFixed(0)} P{s.pfr_pct.toFixed(0)} A{s.af.toFixed(1)}
                </span>
                <span className="badge-hands">{s.hands_seen}手</span>
              </div>
            );
          })}
      </div>

      {waitingNextHand && (
        <div className="hud-turn-indicator" style={{ background: 'rgba(100,149,237,0.2)' }}>
          <span>⏳ 本手进行中，请等待下一手发牌</span>
        </div>
      )}
      {isMyTurn && inCurrentHand && gameState.phase !== 'handOver' && (
        <div className="hud-turn-indicator">
          <span>🎯 轮到你了！</span>
        </div>
      )}
    </div>
  );
};

export default GameHUD;
