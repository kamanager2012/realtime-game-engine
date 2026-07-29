import React, { useCallback, useEffect, useState } from 'react';
import { useGameStore } from '../store/gameStore';
import { getApiBaseUrl } from '../utils/network';
import { PlayerStats } from '../types/game';
import TournamentPanel from './TournamentPanel';
import SpectatorView from './SpectatorView';
import HandHistoryBrowser from './HandHistoryBrowser';
import AdminPanel from './AdminPanel';
import MatchmakingPanel from './MatchmakingPanel';

interface Props {
  onJoinTable: (tableId: string) => void;
  onJoinWithBots: (tableId: string, botCount: number) => void;
  onLogout: () => void;
  playerName: string;
}

interface TableInfo {
  id: string;
  name: string;
  gameType: string;
  sb: number;
  bb: number;
  ante: number;
  players: number;
  max: number;
  status: string;
}

interface LeaderboardEntry {
  player_id: number;
  name: string;
  display_name: string;
  hands_played: number;
  vpip_pct: number;
  pfr_pct: number;
  af: number;
  win_rate: number;
  total_net: number;
  avg_bb_per_100: number;
}

type GameTypeFilter = 'all' | 'NLHE';
type BlindsFilter = 'all' | 'micro' | 'low' | 'small' | 'mid' | 'high';
type PlayersFilter = 'all' | 'hasSeat' | 'almostFull' | 'full';

const TABLE_LABELS: Record<string, string> = {
  main: '主桌',
  table_1: '桌1',
  micro: '微注桌',
  small: '小注桌',
  mid: '中注桌',
  high: '高注桌',
};

const BLINDS_LABELS: Record<BlindsFilter, string> = {
  all: '全部盲注',
  micro: '微注 0.5/1',
  low: '低注 1/2',
  small: '小注 2/4',
  mid: '中注 5/10',
  high: '高注 10/20',
};

function blindsBucket(t: TableInfo): BlindsFilter {
  if (t.bb <= 1) return 'micro';
  if (t.bb <= 2) return 'low';
  if (t.bb <= 4) return 'small';
  if (t.bb <= 10) return 'mid';
  return 'high';
}

function tableDisplayName(t: TableInfo): string {
  const base = TABLE_LABELS[t.id] ?? t.name ?? t.id;
  return `${base} · ${t.sb}/${t.bb} ${t.gameType}`;
}

type Tab = 'tables' | 'leaderboard' | 'tournaments' | 'spectator' | 'history' | 'admin' | 'match';

const Lobby: React.FC<Props> = ({ onJoinTable, onJoinWithBots, onLogout, playerName }) => {
  const token = useGameStore((s) => s.token);
  const playerId = useGameStore((s) => s.playerId);
  const accountChips = useGameStore((s) => s.accountChips);
  const refreshAccountChips = useGameStore((s) => s.refreshAccountChips);
  const [tab, setTab] = useState<Tab>('tables');
  const [tables, setTables] = useState<TableInfo[]>([]);
  const [filterGameType, setFilterGameType] = useState<GameTypeFilter>('all');
  const [filterBlinds, setFilterBlinds] = useState<BlindsFilter>('all');
  const [filterPlayers, setFilterPlayers] = useState<PlayersFilter>('all');
  const [myStats, setMyStats] = useState<PlayerStats | null>(null);
  const [leaderboard, setLeaderboard] = useState<LeaderboardEntry[]>([]);

  // Fetch tables
  useEffect(() => {
    const fetchTables = async () => {
      try {
        const res = await fetch(`${getApiBaseUrl()}/api/tables`);
        if (!res.ok) return;
        const data = (await res.json()) as {
          tables?: Array<{
            id: string;
            name: string;
            game_type: string;
            sb: number;
            bb: number;
            ante: number;
            occupied: number;
            max: number;
            status: string;
          }>;
        };
        if (!data.tables?.length) return;
        setTables(
          data.tables.map((t) => ({
            id: t.id,
            name: t.name,
            gameType: t.game_type || 'NLHE',
            sb: t.sb,
            bb: t.bb,
            ante: t.ante,
            players: t.occupied,
            max: t.max,
            status: t.status,
          }))
        );
      } catch {
        /* keep last known state */
      }
    };
    fetchTables();
    const timer = setInterval(fetchTables, 5000);
    return () => clearInterval(timer);
  }, []);

  useEffect(() => {
    if (token) void refreshAccountChips();
  }, [token, refreshAccountChips]);

  // Fetch my stats
  useEffect(() => {
    if (!playerId || playerId <= 0) return;
    fetch(`${getApiBaseUrl()}/api/players/${playerId}/stats`)
      .then((r) => (r.ok ? r.json() : null))
      .then((s) => s && s.hands_seen > 0 && setMyStats(s))
      .catch(() => {});
  }, [playerId]);

  // Fetch leaderboard
  useEffect(() => {
    if (tab !== 'leaderboard') return;
    fetch(`${getApiBaseUrl()}/api/leaderboard?limit=25`)
      .then((r) => (r.ok ? r.json() : []))
      .then((d) => Array.isArray(d) && setLeaderboard(d))
      .catch(() => {});
  }, [tab]);

  const joinTable = useCallback(
    (tableId: string) => {
      if (!token) return;
      onJoinTable(tableId);
    },
    [token, onJoinTable]
  );

  const joinWithBots = useCallback(
    (tableId: string) => {
      if (!token) return;
      onJoinWithBots(tableId, 0);
    },
    [token, onJoinWithBots]
  );

  const netColor = (n: number) => (n > 0 ? '#4ade80' : n < 0 ? '#ef4444' : '#aaa');

  // Lobby filters
  const filteredTables = tables.filter((t) => {
    if (filterGameType !== 'all' && t.gameType !== filterGameType) return false;
    if (filterBlinds !== 'all' && blindsBucket(t) !== filterBlinds) return false;
    if (filterPlayers === 'hasSeat' && t.players >= t.max) return false;
    if (filterPlayers === 'almostFull' && !(t.players >= t.max * 0.8 && t.players < t.max)) return false;
    if (filterPlayers === 'full' && t.players < t.max) return false;
    return true;
  });

  return (
    <div className="lobby-container">
      <div className="lobby-header">
        {accountChips != null && <div className="lobby-wallet">钱包余额：💰 {accountChips}</div>}
        <h2>🎰 牌桌大厅</h2>
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <span style={{ color: '#ffd700' }}>{playerName}</span>
          <button className="btn-leave" onClick={onLogout}>退出</button>
        </div>
      </div>

      {/* Tab navigation */}
      <div className="lobby-tabs">
        <button className={`lobby-tab ${tab === 'tables' ? 'active' : ''}`} onClick={() => setTab('tables')}>
          🎰 牌桌
        </button>
        <button className={`lobby-tab ${tab === 'leaderboard' ? 'active' : ''}`} onClick={() => setTab('leaderboard')}>
          🏆 排行榜
        </button>
        <button className={`lobby-tab ${tab === 'tournaments' ? 'active' : ''}`} onClick={() => setTab('tournaments')}>
          🏟 锦标赛
        </button>
        <button className={`lobby-tab ${tab === 'spectator' ? 'active' : ''}`} onClick={() => setTab('spectator')}>
          👁 观战
        </button>
        <button className={`lobby-tab ${tab === 'history' ? 'active' : ''}`} onClick={() => setTab('history')}>
          📋 历史
        </button>
        <button className={`lobby-tab ${tab === 'admin' ? 'active' : ''}`} onClick={() => setTab('admin')}>
          🛡 管理
        </button>
        <button className={`lobby-tab ${tab === 'match' ? 'active' : ''}`} onClick={() => setTab('match')}>
          ⚔ Match
        </button>
      </div>

      {/* Tables tab */}
      {tab === 'tables' && (
        <>
          <div className="lobby-filters">
            <label className="filter-group">
              <span>盲注</span>
              <select value={filterBlinds} onChange={(e) => setFilterBlinds(e.target.value as BlindsFilter)}>
                {(Object.keys(BLINDS_LABELS) as BlindsFilter[]).map((k) => (
                  <option key={k} value={k}>{BLINDS_LABELS[k]}</option>
                ))}
              </select>
            </label>
            <label className="filter-group">
              <span>类型</span>
              <select value={filterGameType} onChange={(e) => setFilterGameType(e.target.value as GameTypeFilter)}>
                <option value="all">全部类型</option>
                <option value="NLHE">NLHE 无限注德州</option>
              </select>
            </label>
            <label className="filter-group">
              <span>人数</span>
              <select value={filterPlayers} onChange={(e) => setFilterPlayers(e.target.value as PlayersFilter)}>
                <option value="all">全部</option>
                <option value="hasSeat">有空位</option>
                <option value="almostFull">即将满员</option>
                <option value="full">已满</option>
              </select>
            </label>
            <span className="filter-count">共 {filteredTables.length} 桌</span>
          </div>

          <div className="lobby-tables">
            {filteredTables.length === 0 ? (
              <p style={{ color: '#888', textAlign: 'center', padding: 40 }}>没有符合筛选条件的牌桌。</p>
            ) : (
              filteredTables.map((table) => {
                const isFull = table.players >= table.max;
                return (
                  <div key={table.id} className="table-card">
                    <div className="table-card-header">
                      <h3>{tableDisplayName(table)}</h3>
                    <span className={`table-status ${isFull ? 'full' : 'open'}`}>
                      {isFull ? '已满' : '开放中'}
                    </span>
                  </div>
                  <div className="table-meta">
                    <div className="players-bar">
                      <div className="players-fill" style={{ width: `${(table.players / table.max) * 100}%` }} />
                    </div>
                    <span className="players-count">{table.players} / {table.max} 人</span>
                  </div>
                  <div style={{ display: 'flex', gap: 8 }}>
                    <button className="btn-join" disabled={isFull} onClick={() => joinWithBots(table.id)} style={{ flex: 1 }}>
                      🤖 人机模式
                    </button>
                    <button className="btn-join" disabled={isFull} onClick={() => joinTable(table.id)} style={{ flex: 1 }}>
                      👥 多人模式
                    </button>
                  </div>
                </div>
              );
            })
            )}
          </div>

          <div className="player-stats-section">
            <h3>📊 我的数据</h3>
            <div className="stats-grid">
              <div className="stat-item">
                <span className="stat-label">总手牌</span>
                <span className="stat-value">{myStats?.hands_seen ?? 0}</span>
              </div>
              <div className="stat-item">
                <span className="stat-label">胜率</span>
                <span className="stat-value">{myStats ? `${myStats.win_rate.toFixed(1)}%` : '--%'}</span>
              </div>
              <div className="stat-item">
                <span className="stat-label">总盈利</span>
                <span className="stat-value" style={{ color: netColor(myStats?.total_net ?? 0) }}>
                  {myStats ? myStats.total_net.toFixed(0) : '0'}
                </span>
              </div>
              <div className="stat-item">
                <span className="stat-label">VPIP</span>
                <span className="stat-value">{myStats ? `${myStats.vpip_pct.toFixed(0)}%` : '--%'}</span>
              </div>
              <div className="stat-item">
                <span className="stat-label">PFR</span>
                <span className="stat-value">{myStats ? `${myStats.pfr_pct.toFixed(0)}%` : '--%'}</span>
              </div>
              <div className="stat-item">
                <span className="stat-label">AF</span>
                <span className="stat-value">{myStats ? myStats.af.toFixed(1) : '--'}</span>
              </div>
              <div className="stat-item">
                <span className="stat-label">BB/100</span>
                <span className="stat-value" style={{ color: netColor(myStats?.avg_bb_per_100 ?? 0) }}>
                  {myStats ? myStats.avg_bb_per_100.toFixed(1) : '--'}
                </span>
              </div>
              <div className="stat-item">
                <span className="stat-label">手牌赢</span>
                <span className="stat-value">{myStats?.hands_won ?? 0}</span>
              </div>
            </div>
          </div>
        </>
      )}

      {/* Leaderboard tab */}
      {tab === 'tournaments' && <TournamentPanel />}
      {tab === 'spectator' && <SpectatorView />}
      {tab === 'history' && <HandHistoryBrowser />}
      {tab === 'admin' && <AdminPanel />}
      {tab === 'match' && <MatchmakingPanel />}

      {tab === 'leaderboard' && (
        <div className="leaderboard-section">
          {leaderboard.length === 0 ? (
            <p style={{ color: '#888', textAlign: 'center', padding: 40 }}>暂无数据。开始游戏后排行榜将显示玩家排名。</p>
          ) : (
            <table className="leaderboard-table">
              <thead>
                <tr>
                  <th>#</th>
                  <th>玩家</th>
                  <th>手牌</th>
                  <th>VPIP</th>
                  <th>PFR</th>
                  <th>AF</th>
                  <th>胜率</th>
                  <th>BB/100</th>
                  <th>盈利</th>
                </tr>
              </thead>
              <tbody>
                {leaderboard.map((e, i) => (
                  <tr key={e.player_id} className={e.player_id === playerId ? 'self-row' : ''}>
                    <td className="rank">{i === 0 ? '🥇' : i === 1 ? '🥈' : i === 2 ? '🥉' : i + 1}</td>
                    <td className="name">{e.display_name || e.name}</td>
                    <td>{e.hands_played}</td>
                    <td>{e.vpip_pct.toFixed(0)}%</td>
                    <td>{e.pfr_pct.toFixed(0)}%</td>
                    <td>{e.af.toFixed(1)}</td>
                    <td>{(e.win_rate * 100).toFixed(1)}%</td>
                    <td style={{ color: netColor(e.avg_bb_per_100) }}>{e.avg_bb_per_100.toFixed(1)}</td>
                    <td style={{ color: netColor(e.total_net) }}>{e.total_net.toFixed(0)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      )}
    </div>
  );
};

export default Lobby;
