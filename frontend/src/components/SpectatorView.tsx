import React, { useCallback, useEffect, useRef, useState } from 'react';
import { getApiBaseUrl } from '../utils/network';

// ========== Types ==========

interface SpectatorPlayer {
  id: number;
  name: string;
  chips: number;
  active: boolean;
  eliminated: boolean;
}

interface TournamentState {
  id: number;
  name: string;
  status: string;
  type: string;
  buy_in: number;
  active_players: number;
  max_players: number;
  prize_pool: number;
  current_blinds: string;
  players?: SpectatorPlayer[];
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

// ========== Component ==========

const SpectatorView: React.FC = () => {
  const [tournaments, setTournaments] = useState<TournamentState[]>([]);
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [detail, setDetail] = useState<TournamentState | null>(null);
  const [chatMessages, setChatMessages] = useState<Array<{ name: string; text: string; time: string }>>([]);
  const [chatInput, setChatInput] = useState('');
  const chatEndRef = useRef<HTMLDivElement>(null);

  // Fetch tournament list
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

  // Poll selected tournament detail
  useEffect(() => {
    if (!selectedId) { setDetail(null); return; }
    const poll = async () => {
      try {
        const res = await fetch(`${getApiBaseUrl()}/api/spectator/${selectedId}`);
        if (res.ok) {
          const data = await res.json();
          if (data && data.id) setDetail(data);
        }
      } catch {}
    };
    poll();
    const t = setInterval(poll, 2000);
    return () => clearInterval(t);
  }, [selectedId]);

  // Auto-scroll chat
  useEffect(() => { chatEndRef.current?.scrollIntoView({ behavior: 'smooth' }); }, [chatMessages]);

  const sendChat = useCallback(() => {
    if (!chatInput.trim()) return;
    setChatMessages((prev) => [
      ...prev.slice(-99),
      { name: '你', text: chatInput.trim(), time: new Date().toLocaleTimeString() },
    ]);
    setChatInput('');
  }, [chatInput]);

  // Tournament list view
  if (!selectedId) {
    return (
      <div className="spectator-panel">
        <h3>👁 观战模式</h3>
        {tournaments.length === 0 ? (
          <p style={{ color: '#888', textAlign: 'center', padding: 40 }}>暂无进行中的锦标赛</p>
        ) : (
          <div className="spectator-tournament-list">
            {tournaments.map((t) => (
              <div key={t.id} className="spectator-tournament-card" onClick={() => setSelectedId(t.id)}>
                <div className="spectator-tournament-name">{t.name}</div>
                <div className="spectator-tournament-status">{STATUS_LABELS[t.status] || t.status}</div>
                <button className="btn-watch">👁 观看</button>
              </div>
            ))}
          </div>
        )}
      </div>
    );
  }

  // Detail view
  return (
    <div className="spectator-view">
      <div className="spectator-header">
        <button className="btn-back" onClick={() => setSelectedId(null)}>← 返回列表</button>
        <h3>👁 {detail?.name || `锦标赛 #${selectedId}`}</h3>
        <div className="spectator-meta">
          {detail && (
            <>
              <span>🏆 ${detail.prize_pool}</span>
              <span>👥 {detail.active_players}/{detail.max_players}</span>
              <span>🪙 {detail.current_blinds}</span>
              <span className={`tournament-status status-${detail.status}`}>
                {STATUS_LABELS[detail.status] || detail.status}
              </span>
            </>
          )}
        </div>
      </div>

      {/* Players */}
      <div className="spectator-table">
        {detail?.players && detail.players.length > 0 ? (
          <div className="spectator-players">
            {detail.players.map((p, i) => (
              <div key={p.id || i} className={`spectator-player ${!p.active ? 'eliminated' : ''}`}>
                <span className="sp-rank">{i + 1}</span>
                <span className="sp-name">{p.name || `P${p.id}`}</span>
                <span className="sp-chips">💰 {p.chips}</span>
                {p.eliminated && <span className="sp-out">出局</span>}
              </div>
            ))}
          </div>
        ) : (
          <div className="spectator-waiting">
            <p>加载中...</p>
          </div>
        )}
      </div>

      {/* Chat */}
      <div className="spectator-chat">
        <div className="chat-messages">
          {chatMessages.map((c, i) => (
            <div key={i} className="chat-msg spectator">
              <span className="chat-name">👁 {c.name}</span>
              <span className="chat-text">{c.text}</span>
              <span className="chat-time">{c.time}</span>
            </div>
          ))}
          <div ref={chatEndRef} />
        </div>
        <div className="chat-input-row">
          <input
            value={chatInput}
            onChange={(e) => setChatInput(e.target.value)}
            onKeyDown={(e) => e.key === 'Enter' && sendChat()}
            placeholder="发送消息..."
            maxLength={200}
          />
          <button onClick={sendChat}>发送</button>
        </div>
      </div>
    </div>
  );
};

export default SpectatorView;
