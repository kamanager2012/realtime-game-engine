import React, { useEffect, useState, useRef } from 'react';
import { useGameStore } from '../store/gameStore';
import { useWebSocket } from '../hooks/useWebSocket';

const ChatPanel: React.FC = () => {
  const messages = useGameStore((s) => s.chatMessages);
  const playerName = useGameStore((s) => s.playerName);
  const currentTableId = useGameStore((s) => s.currentTableId);
  const { sendChat } = useWebSocket();
  const [input, setInput] = useState('');
  const [isOpen, setIsOpen] = useState(true);
  const messagesEndRef = useRef<HTMLDivElement>(null);

  const handleSend = () => {
    if (!input.trim() || !currentTableId) return;
    sendChat(input.trim(), currentTableId);
    setInput('');
  };

  useEffect(() => {
    messagesEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages]);

  if (!isOpen) {
    return (
      <button className="chat-toggle-btn" onClick={() => setIsOpen(true)} aria-label="打开聊天">
        💬
      </button>
    );
  }

  return (
    <div className="chat-panel">
      <div className="chat-header">
        <span>💬 聊天</span>
        <button className="chat-close" onClick={() => setIsOpen(false)} aria-label="关闭聊天">✕</button>
      </div>
      <div className="chat-messages">
        {messages.length === 0 && <div className="chat-empty">暂无消息</div>}
        {messages.map((msg, idx) => (
          <div key={idx} className={`chat-message ${msg.player === playerName ? 'self' : ''}`}>
            <span className="chat-player">{msg.player === playerName ? '你' : msg.player}:</span>
            <span className="chat-text">{msg.message}</span>
          </div>
        ))}
        <div ref={messagesEndRef} />
      </div>
      <div className="chat-input">
        <input
          type="text"
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={(e) => { if (e.key === 'Enter') handleSend(); }}
          placeholder="说点什么..."
        />
        <button onClick={handleSend} aria-label="发送">发送</button>
      </div>
    </div>
  );
};

export default ChatPanel;
