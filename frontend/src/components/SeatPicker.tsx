import React from 'react';
import { PlayerState } from '../types/game';

interface Props {
  players: PlayerState[];
  pendingSeatIndex: number | null;
  isSeating: boolean;
  onSelectSeat: (seatIndex: number) => void;
}

const SeatPicker: React.FC<Props> = ({ players, pendingSeatIndex, isSeating, onSelectSeat }) => {
  const seats = Array.from({ length: 6 }, (_, i) => players[i] ?? { occupied: false, seat_index: i });

  return (
    <div className="seat-picker" style={{ marginTop: 12 }}>
      <p style={{ textAlign: 'center', color: '#ffd700', marginBottom: 10, fontSize: 14 }}>
        请选择座位（单选）
      </p>
      <div style={{ display: 'flex', justifyContent: 'center', gap: 10, flexWrap: 'wrap' }}>
        {seats.map((player, idx) => {
          const taken = !!player.occupied;
          const pending = pendingSeatIndex === idx;
          const disabled = taken || isSeating;
          return (
            <button
              key={idx}
              type="button"
              disabled={disabled}
              onClick={() => onSelectSeat(idx)}
              style={{
                minWidth: 72,
                padding: '10px 12px',
                borderRadius: 10,
                border: pending ? '2px solid #ffd700' : '1px solid rgba(255,255,255,0.2)',
                background: taken ? 'rgba(120,120,120,0.35)' : pending ? 'rgba(255,215,0,0.2)' : 'rgba(46,125,50,0.55)',
                color: taken ? '#aaa' : '#fff',
                cursor: disabled ? 'not-allowed' : 'pointer',
                fontWeight: 700,
              }}
            >
              座位 {idx + 1}
              <div style={{ fontSize: 11, fontWeight: 400, marginTop: 4 }}>
                {taken ? '已占用' : pending ? '入座中...' : '空位'}
              </div>
            </button>
          );
        })}
      </div>
    </div>
  );
};

export default SeatPicker;
