import React, { useEffect, useState, useCallback } from 'react';
import { useGameStore } from '../store/gameStore';
import { useWebSocket } from '../hooks/useWebSocket';

const DailyBonus: React.FC = () => {
  const token = useGameStore((s) => s.token);
  const addToast = useGameStore((s) => s.addToast);
  const refreshAccountChips = useGameStore((s) => s.refreshAccountChips);
  const [open, setOpen] = useState(false);
  const [reward, setReward] = useState(0);
  const [streak, setStreak] = useState(0);

  const load = useCallback(() => {
    if (!token) return;
    fetch('/api/daily-bonus', { headers: { Authorization: `Bearer ${token}` } })
      .then((r) => r.json())
      .then((j) => {
        if (j.can_claim) {
          setReward(j.reward ?? 0);
          setStreak(j.streak ?? 0);
          setOpen(true);
        }
      })
      .catch(() => {});
  }, [token]);

  // 登录后或 token 变化时检查
  useEffect(() => {
    load();
  }, [load]);

  const claim = useCallback(() => {
    if (!token) return;
    fetch('/api/daily-bonus/claim', {
      method: 'POST',
      headers: { Authorization: `Bearer ${token}` },
    })
      .then((r) => r.json())
      .then((j) => {
        if (j.reward) {
          addToast('success', `每日奖励 +${j.reward} 筹码（连续 ${j.streak} 天）`);
          void refreshAccountChips();
        } else {
          addToast('info', '今日已领取');
        }
        setOpen(false);
      })
      .catch(() => setOpen(false));
  }, [token, addToast, refreshAccountChips]);

  if (!open) return null;

  return (
    <div className="modal-overlay" onClick={() => setOpen(false)}>
      <div className="modal daily-bonus-modal" onClick={(e) => e.stopPropagation()}>
        <h2>🎁 每日奖励</h2>
        <p>
          连续登录 <b>{streak + 1}</b> 天，领取 <b>💰 {reward}</b> 免费筹码！
        </p>
        <div className="modal-actions">
          <button className="btn-primary" onClick={claim}>领取</button>
          <button className="btn-secondary" onClick={() => setOpen(false)}>明天再来</button>
        </div>
      </div>
    </div>
  );
};

export default DailyBonus;
