import React, { useEffect, useState } from 'react';
import { useGameStore } from '../store/gameStore';
import { getApiBaseUrl } from '../utils/network';

interface TournamentInfo {
  id: number;
  name: string;
  status: string;
  type: string;
  buy_in: number;
  players: number;
  max_players: number;
  prize_pool: number;
  current_blinds: string;
}

const STATUS_LABELS: Record<string, string> = {
  Registration: '报名中',
  Running: '进行中',
  LateRegistration: '延迟报名',
  OnBreak: '休息',
  FinalTable: '决赛桌',
  Completed: '已结束',
  Cancelled: '已取消',
};

const TournamentPanel: React.FC = () => {
  const playerId = useGameStore((s) => s.playerId);
  const playerName = useGameStore((s) => s.playerName);
  const [tournaments, setTournaments] = useState<TournamentInfo[]>([]);
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [detail, setDetail] = useState<any>(null);

  useEffect(() => {
    const fetchList = async () => {
      try {
        const res = await fetch(`${getApiBaseUrl()}/api/tournaments`);
        if (res.ok) {
          const data = await res.json();
          if (Array.isArray(data)) setTournaments(data);
        }
      } catch {}
    };
    fetchList();
    const t = setInterval(fetchList, 10000);
    return () => clearInterval(t);
  }, []);

  useEffect(() => {
    if (!selectedId) { setDetail(null); return; }
    fetch(`${getApiBaseUrl()}/api/tournaments/${selectedId}`)
      .then((r) => (r.ok ? r.json() : null))
      .then((d) => d && setDetail(d))
      .catch(() => {});
  }, [selectedId, tournaments]);

  const joinTournament = async (tid: number) => {
    if (!playerId || playerId <= 0) return;
    try {
      const res = await fetch(`${getApiBaseUrl()}/api/tournaments/join`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ tournament_id: tid, player_id: playerId, name: playerName }),
      });
      const data = await res.json();
      if (data.result === 'ok') {
        setSelectedId(tid);
      }
    } catch {}
  };

  return (
    <div className="tournament-panel">
      <h3>🏆 锦标赛</h3>
      {tournaments.length === 0 ? (
        <p style={{ color: '#888', padding: 16, textAlign: 'center' }}>暂无锦标赛</p>
      ) : (
        <div className="tournament-list">
          {tournaments.map((t) => (
            <div
              key={t.id}
              className={`tournament-card ${selectedId === t.id ? 'selected' : ''}`}
              onClick={() => setSelectedId(t.id)}
            >
              <div className="tournament-header">
                <span className="tournament-name">{t.name}</span>
                <span className={`tournament-status status-${t.status}`}>
                  {STATUS_LABELS[t.status] || t.status}
                </span>
              </div>
              <div className="tournament-meta">
                <span>💰 ${t.buy_in}</span>
                <span>👥 {t.players}/{t.max_players}</span>
                <span>🏆 ${t.prize_pool}</span>
              </div>
              {t.status === 'Registration' && (
                <button
                  className="btn-join-tournament"
                  onClick={(e) => { e.stopPropagation(); joinTournament(t.id); }}
                >
                  报名参加
                </button>
              )}
            </div>
          ))}
        </div>
      )}

      {detail && (
        <div className="tournament-detail">
          <h4>{detail.name || `锦标赛 #${selectedId}`}</h4>
          <div className="detail-grid">
            <div className="detail-item"><span className="detail-label">状态</span><span>{STATUS_LABELS[detail.status] || detail.status}</span></div>
            <div className="detail-item"><span className="detail-label">类型</span><span>{detail.type || 'Freezeout'}</span></div>
            <div className="detail-item"><span className="detail-label">买入</span><span>${detail.buy_in || 0}</span></div>
            <div className="detail-item"><span className="detail-label">人数</span><span>{detail.active_players || 0}/{detail.max_players || 0}</span></div>
            <div className="detail-item"><span className="detail-label">奖金池</span><span>${detail.prize_pool || 0}</span></div>
            <div className="detail-item"><span className="detail-label">盲注</span><span>{detail.current_blinds || '--'}</span></div>
          </div>
          {detail.players && Array.isArray(detail.players) && detail.players.length > 0 && (
            <div className="tournament-players">
              <h5>参赛选手</h5>
              {detail.players.map((p: any, i: number) => (
                <div key={i} className="tp-row">
                  <span className="tp-rank">{i + 1}</span>
                  <span className="tp-name">{p.name || `P${p.id}`}</span>
                  <span className="tp-chips">💰 {p.chips || 0}</span>
                  {p.eliminated && <span className="tp-out">出局</span>}
                </div>
              ))}
            </div>
          )}
        </div>
      )}
    </div>
  );
};

export default TournamentPanel;
