import React from 'react';
import { PlayerState } from '../types/game';
import { cardToString, hasDealtHoleCards } from '../utils/cards';

interface Props {
  player: PlayerState;
  isSelf: boolean;
  isCurrentPlayer: boolean;
  onClick: () => void;
}

const PlayerPanel: React.FC<Props> = ({ player, isSelf, isCurrentPlayer, onClick }) => {
  const statusColors: Record<string, string> = {
    active: '#22c55e',
    folded: '#ef4444',
    all_in: '#f97316',
    sitting_out: '#6b7280',
    disconnected: '#9ca3af',
  };

  const statusLabels: Record<string, string> = {
    active: '活跃',
    folded: '已弃牌',
    all_in: '全押!',
    sitting_out: '旁观中',
    disconnected: '已掉线',
  };

  const suits = ['♠', '♥', '♦', '♣'];
  const ranks = ['A', '2', '3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K'];

  return (
    <div className={`player-panel ${isSelf ? 'self' : ''} ${isCurrentPlayer ? 'current' : ''}`} onClick={onClick}>
      <div className="player-avatar">
        <div className="avatar-placeholder">
          {player.display_name?.[0]?.toUpperCase() || '?'}
        </div>
        {isCurrentPlayer && <div className="turn-indicator" />}
      </div>
      <div className="player-info">
        <div className="player-name-row">
          <span className="player-name">{player.display_name || `玩家 ${player.player_id}`}</span>
          {isSelf && <span className="self-badge">你</span>}
        </div>
        <div className="player-stats">
          <span className="chips">💰 {player.chips.toLocaleString()}</span>
          {player.bet_this_round > 0 && <span className="bet-amount">下注: {player.bet_this_round}</span>}
        </div>
        <div className="player-status">
          <span className="status-dot" style={{ background: statusColors[player.status] || '#666' }} />
          <span className="status-text">{statusLabels[player.status] || player.status}</span>
        </div>
      </div>
      {isSelf && hasDealtHoleCards(player.hole_cards) && (
        <div className="hole-cards-preview">
          {player.hole_cards.map((card, i) => {
            const suit = suits[Math.floor(card / 13)];
            const rank = ranks[card % 13];
            return <span key={i} className="mini-card">{rank}{suit}</span>;
          })}
        </div>
      )}
      <div className="seat-badge">#{player.seat_index + 1}</div>
    </div>
  );
};

export default PlayerPanel;
