import React, { useEffect, useState } from 'react';
import { getApiBaseUrl } from '../utils/network';

interface ServerStats {
  tables: number;
  db_healthy: boolean;
  tournament_running: boolean;
  hand_count: number;
  player_count: number;
  account_count: number;
}

interface Account {
  id: number;
  username: string;
  display_name: string;
  chips: number;
  elo: number;
  hands: number;
  created: string;
  last_login: string;
}

type AdminTab = 'overview' | 'accounts' | 'anticheat';

const AdminPanel: React.FC = () => {
  const [tab, setTab] = useState<AdminTab>('overview');
  const [stats, setStats] = useState<ServerStats | null>(null);
  const [accounts, setAccounts] = useState<Account[]>([]);
  const [alerts, setAlerts] = useState<any[]>([]);

  // Fetch server stats
  useEffect(() => {
    if (tab !== 'overview') return;
    const poll = async () => {
      try {
        const res = await fetch(`${getApiBaseUrl()}/api/admin/stats`);
        if (res.ok) setStats(await res.json());
      } catch {}
    };
    poll();
    const t = setInterval(poll, 5000);
    return () => clearInterval(t);
  }, [tab]);

  // Fetch accounts
  useEffect(() => {
    if (tab !== 'accounts') return;
    fetch(`${getApiBaseUrl()}/api/admin/players`)
      .then((r) => (r.ok ? r.json() : []))
      .then((d) => Array.isArray(d) && setAccounts(d))
      .catch(() => {});
  }, [tab]);

  // Fetch anticheat alerts
  useEffect(() => {
    if (tab !== 'anticheat') return;
    fetch(`${getApiBaseUrl()}/anticheat/alerts?limit=20`)
      .then((r) => (r.ok ? r.json() : {}))
      .then((d: any) => setAlerts(d.alerts || []))
      .catch(() => {});
  }, [tab]);

  const levelColors: Record<string, string> = {
    clean: '#4ade80', low: '#3b82f6', medium: '#f59e0b', high: '#ef4444', confirmed: '#a855f7',
  };

  return (
    <div className="admin-panel">
      <h3>🛡️ 管理面板</h3>
      <div className="lobby-tabs">
        <button className={`lobby-tab ${tab === 'overview' ? 'active' : ''}`} onClick={() => setTab('overview')}>
          服务器
        </button>
        <button className={`lobby-tab ${tab === 'accounts' ? 'active' : ''}`} onClick={() => setTab('accounts')}>
          账户
        </button>
        <button className={`lobby-tab ${tab === 'anticheat' ? 'active' : ''}`} onClick={() => setTab('anticheat')}>
          反作弊
        </button>
      </div>

      {/* Overview */}
      {tab === 'overview' && (
        <div className="admin-overview">
          {stats ? (
            <div className="admin-stats-grid">
              <div className="admin-stat-card">
                <div className="admin-stat-value">{stats.tables}</div>
                <div className="admin-stat-label">牌桌</div>
              </div>
              <div className="admin-stat-card">
                <div className="admin-stat-value">{stats.account_count}</div>
                <div className="admin-stat-label">账户</div>
              </div>
              <div className="admin-stat-card">
                <div className="admin-stat-value">{stats.player_count}</div>
                <div className="admin-stat-label">玩家</div>
              </div>
              <div className="admin-stat-card">
                <div className="admin-stat-value">{stats.hand_count}</div>
                <div className="admin-stat-label">手牌</div>
              </div>
              <div className="admin-stat-card">
                <div className={`admin-stat-value ${stats.db_healthy ? 'healthy' : 'unhealthy'}`}>
                  {stats.db_healthy ? '正常' : '异常'}
                </div>
                <div className="admin-stat-label">数据库</div>
              </div>
              <div className="admin-stat-card">
                <div className={`admin-stat-value ${stats.tournament_running ? 'healthy' : 'idle'}`}>
                  {stats.tournament_running ? '运行中' : '空闲'}
                </div>
                <div className="admin-stat-label">锦标赛</div>
              </div>
            </div>
          ) : (
            <p style={{ color: '#888', textAlign: 'center', padding: 40 }}>加载中...</p>
          )}
        </div>
      )}

      {/* Accounts */}
      {tab === 'accounts' && (
        <div className="admin-accounts">
          {accounts.length === 0 ? (
            <p style={{ color: '#888', textAlign: 'center', padding: 24 }}>暂无注册账户</p>
          ) : (
            <table className="hh-table">
              <thead>
                <tr>
                  <th>ID</th>
                  <th>用户名</th>
                  <th>显示名</th>
                  <th>筹码</th>
                  <th>ELO</th>
                  <th>手牌</th>
                  <th>最后登录</th>
                </tr>
              </thead>
              <tbody>
                {accounts.map((a) => (
                  <tr key={a.id}>
                    <td className="id-cell">#{a.id}</td>
                    <td className="name">{a.username}</td>
                    <td>{a.display_name || '—'}</td>
                    <td className="pot">{a.chips}</td>
                    <td>{a.elo}</td>
                    <td>{a.hands}</td>
                    <td className="time-cell">{a.last_login?.slice(0, 19) || '—'}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      )}

      {/* Anticheat */}
      {tab === 'anticheat' && (
        <div className="admin-anticheat">
          {alerts.length === 0 ? (
            <p style={{ color: '#4ade80', textAlign: 'center', padding: 24 }}>无告警 — 系统安全</p>
          ) : (
            <table className="hh-table">
              <thead>
                <tr>
                  <th>玩家</th>
                  <th>级别</th>
                  <th>评分</th>
                  <th>原因</th>
                </tr>
              </thead>
              <tbody>
                {alerts.map((alert: any, i: number) => (
                  <tr key={i}>
                    <td className="name">{alert.player_name} (#{alert.player_id})</td>
                    <td>
                      <span className="alert-badge" style={{ background: levelColors[alert.level] || '#666' }}>
                        {alert.level}
                      </span>
                    </td>
                    <td>{alert.score?.toFixed(1)}</td>
                    <td style={{ maxWidth: 300, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                      {alert.reason}
                    </td>
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

export default AdminPanel;
