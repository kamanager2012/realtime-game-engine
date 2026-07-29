import React, { useEffect, useState, useCallback } from 'react';
import { useGameStore } from '../store/gameStore';

interface Achievement {
  id: string;
  name: string;
  desc: string;
  icon: string;
  unlocked: boolean;
}

const PlayerInfoModal: React.FC = () => {
  const target = useGameStore((s) => s.playerInfoTarget);
  const token = useGameStore((s) => s.token);
  const setPlayerInfoTarget = useGameStore((s) => s.setPlayerInfoTarget);
  const [achievements, setAchievements] = useState<Achievement[]>([]);
  const [loading, setLoading] = useState(false);

  const close = useCallback(() => setPlayerInfoTarget(null), [setPlayerInfoTarget]);

  useEffect(() => {
    if (!target || !token) {
      setAchievements([]);
      return;
    }
    setLoading(true);
    fetch(`/api/players/${target.player_id}/achievements`, {
      headers: { Authorization: `Bearer ${token}` },
    })
      .then((r) => (r.ok ? r.json() : []))
      .then((j) => setAchievements(Array.isArray(j) ? j : []))
      .catch(() => setAchievements([]))
      .finally(() => setLoading(false));
  }, [target, token]);

  if (!target) return null;

  const displayName = target.display_name || `玩家 ${target.player_id}`;
  const unlockedCount = achievements.filter((a) => a.unlocked).length;

  return (
    <div className="player-info-modal" onClick={close}>
      <div className="player-info-content" onClick={(e) => e.stopPropagation()}>
        <h3>👤 {displayName}</h3>
        <p>💰 桌面筹码：{target.chips}</p>
        <p>状态：{target.status}</p>

        <h3 style={{ marginTop: 16 }}>🏆 成就 ({unlockedCount}/{achievements.length})</h3>
        {loading ? (
          <p>加载中...</p>
        ) : achievements.length === 0 ? (
          <p>暂无成就数据</p>
        ) : (
          <div className="achievement-list">
            {achievements.map((a) => (
              <div
                key={a.id}
                className={['achievement-item', a.unlocked ? 'unlocked' : 'locked'].join(' ')}
                title={a.desc}
              >
                <span className="achievement-icon">{a.unlocked ? a.icon : '🔒'}</span>
                <div className="achievement-text">
                  <div className="achievement-name">{a.name}</div>
                  <div className="achievement-desc">{a.desc}</div>
                </div>
              </div>
            ))}
          </div>
        )}

        <button onClick={close}>关闭</button>
      </div>
    </div>
  );
};

export default PlayerInfoModal;
