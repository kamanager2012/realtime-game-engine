import React, { useState } from 'react';
import { getApiBaseUrl } from '../utils/network';

interface Props {
  onAuthenticated: (token: string, playerId: number, playerName: string) => void;
}

const AuthModal: React.FC<Props> = ({ onAuthenticated }) => {
  const [isLogin, setIsLogin] = useState(true);
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [displayName, setDisplayName] = useState('');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    setLoading(true);
    try {
      const endpoint = isLogin ? '/api/auth/login' : '/api/auth/register';
      const body = isLogin
        ? { username, password }
        : { username, password, display_name: displayName };

      const resp = await fetch(`${getApiBaseUrl()}${endpoint}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });
      const data = await resp.json();
      if (!resp.ok) throw new Error(data.message || (isLogin ? '登录失败' : '注册失败'));
      onAuthenticated(data.token, data.player_id, data.display_name ?? data.username);
    } catch (err: any) {
      setError(err.message || '请求失败');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="auth-overlay">
      <div className="auth-modal">
        <h2>{isLogin ? '👋 登录' : '🆕 注册'}</h2>
        {error && <div className="auth-error">{error}</div>}
        <form onSubmit={handleSubmit}>
          <div className="form-group">
            <label>用户名</label>
            <input type="text" value={username} onChange={(e) => setUsername(e.target.value)} placeholder="3-32个字符" minLength={3} maxLength={32} required />
          </div>
          {!isLogin && (
            <div className="form-group">
              <label>显示名称</label>
              <input type="text" value={displayName} onChange={(e) => setDisplayName(e.target.value)} placeholder="牌桌上的昵称" />
            </div>
          )}
          <div className="form-group">
            <label>密码</label>
            <input type="password" value={password} onChange={(e) => setPassword(e.target.value)} placeholder={isLogin ? '密码' : '至少6位'} minLength={6} required />
          </div>
          <button className="btn-submit" type="submit" disabled={loading}>
            {loading ? '处理中...' : isLogin ? '登录' : '注册'}
          </button>
        </form>
        <div className="auth-toggle">
          {isLogin ? <p>没有账号？<a onClick={() => setIsLogin(false)}>注册</a></p> : <p>已有账号？<a onClick={() => setIsLogin(true)}>登录</a></p>}
        </div>
      </div>
    </div>
  );
};

export default AuthModal;
