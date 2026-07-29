import React from 'react';
import { useGameStore } from '../store/gameStore';

export interface Toast {
  id: string;
  type: 'success' | 'error' | 'info';
  message: string;
  timestamp: number;
}

const ToastContainer: React.FC = () => {
  const toasts = useGameStore((s) => s.toasts);

  return (
    <div className="toast-container">
      {toasts.map((t) => (
        <div key={t.id} className={`toast toast-${t.type}`} role="alert">
          {t.message}
        </div>
      ))}
    </div>
  );
};

export function showToast(message: string, type: Toast['type'] = 'info') {
  const container = document.querySelector('.toast-container');
  if (!container) return;

  const el = document.createElement('div');
  el.className = `toast toast-${type}`;
  el.textContent = message;
  el.setAttribute('role', 'alert');
  container.appendChild(el);

  setTimeout(() => {
    el.classList.add('toast-exit');
    setTimeout(() => el.remove(), 300);
  }, 3000);
}

export default ToastContainer;
