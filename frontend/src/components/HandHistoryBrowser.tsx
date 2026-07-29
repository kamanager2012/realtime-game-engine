import React, { useEffect, useState } from 'react';
import { getApiBaseUrl } from '../utils/network';

interface HandSummary {
  hand_id: number;
  table_id: string;
  hand_number: number;
  community_cards: string;
  pot_amount: number;
  timestamp: string;
}

interface PlayerResult {
  player_id: number;
  name: string;
  hole_cards: string;
  action_summary: string;
  amount_won: number;
  net_profit: number;
  won: number;
  best_hand: string;
}

interface HandDetail extends HandSummary {
  players: PlayerResult[];
}

const HandHistoryBrowser: React.FC = () => {
  const [hands, setHands] = useState<HandSummary[]>([]);
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [detail, setDetail] = useState<HandDetail | null>(null);
  const [loading, setLoading] = useState(false);

  // Fetch recent hands
  useEffect(() => {
    setLoading(true);
    fetch(`${getApiBaseUrl()}/api/hands?limit=30`)
      .then((r) => (r.ok ? r.json() : []))
      .then((data) => { if (Array.isArray(data)) setHands(data); })
      .catch(() => {})
      .finally(() => setLoading(false));
  }, []);

  // Fetch hand detail
  useEffect(() => {
    if (!selectedId) { setDetail(null); return; }
    fetch(`${getApiBaseUrl()}/api/hands/${selectedId}`)
      .then((r) => (r.ok ? r.json() : null))
      .then((d) => d && setDetail(d))
      .catch(() => {});
  }, [selectedId]);

  const profitColor = (n: number) => (n > 0 ? '#4ade80' : n < 0 ? '#ef4444' : '#aaa');

  return (
    <div className="hand-history">
      <h3>📋 手牌历史</h3>

      {loading && <p style={{ color: '#888', textAlign: 'center' }}>加载中...</p>}

      {!loading && hands.length === 0 && (
        <p style={{ color: '#888', textAlign: 'center', padding: 24 }}>
          暂无手牌记录。开始游戏后这里会显示历史手牌。
        </p>
      )}

      {/* Detail view */}
      {detail && (
        <div className="hh-detail">
          <button className="btn-back" onClick={() => { setSelectedId(null); setDetail(null); }}>
            ← 返回列表
          </button>
          <div className="hh-detail-header">
            <span>牌桌: {detail.table_id}</span>
            <span>底池: ${detail.pot_amount.toFixed(0)}</span>
            <span>公共牌: {detail.community_cards || '—'}</span>
            <span>{detail.timestamp}</span>
          </div>
          {detail.players && detail.players.length > 0 && (
            <table className="hh-detail-table">
              <thead>
                <tr>
                  <th>玩家</th>
                  <th>底牌</th>
                  <th>最佳手牌</th>
                  <th>行动</th>
                  <th>盈亏</th>
                </tr>
              </thead>
              <tbody>
                {detail.players.map((p) => (
                  <tr key={p.player_id} className={p.won ? 'winner-row' : ''}>
                    <td className="name">{p.name}</td>
                    <td className="cards">{p.hole_cards || '—'}</td>
                    <td>{p.best_hand || '—'}</td>
                    <td className="actions-cell">{p.action_summary || '—'}</td>
                    <td style={{ color: profitColor(p.net_profit), fontWeight: 600 }}>
                      {p.net_profit > 0 ? '+' : ''}{p.net_profit.toFixed(0)}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      )}

      {/* List view */}
      {!detail && hands.length > 0 && (
        <div className="hh-list">
          <table className="hh-table">
            <thead>
              <tr>
                <th>ID</th>
                <th>牌桌</th>
                <th>底池</th>
                <th>公共牌</th>
                <th>时间</th>
              </tr>
            </thead>
            <tbody>
              {hands.map((h) => (
                <tr key={h.hand_id} onClick={() => setSelectedId(h.hand_id)} className="hh-row">
                  <td className="id-cell">#{h.hand_id}</td>
                  <td>{h.table_id}</td>
                  <td className="pot">${h.pot_amount.toFixed(0)}</td>
                  <td className="cards-cell">{h.community_cards || '—'}</td>
                  <td className="time-cell">{h.timestamp?.slice(11, 19) || '—'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
};

export default HandHistoryBrowser;
