import React, { useState, useEffect, useRef, useCallback } from 'react';
import GameTable from './GameTable';

interface Props {
  handId: number;
  onClose: () => void;
}

const ReplayViewer: React.FC<Props> = ({ handId, onClose }) => {
  const [snapshot, setSnapshot] = useState<any>(null);
  const [isPlaying, setIsPlaying] = useState(false);
  const [isPaused, setIsPaused] = useState(false);
  const [currentTime, setCurrentTime] = useState(0);
  const [totalDuration, setTotalDuration] = useState(1);
  const [playbackSpeed, setPlaybackSpeed] = useState(1);
  const [events, setEvents] = useState<any[]>([]);
  const [currentEventIdx, setCurrentEventIdx] = useState(0);
  const wsRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    const ws = new WebSocket(
      `${window.location.protocol === 'https:' ? 'wss:' : 'ws:'}//${window.location.host}/ws/replay`
    );
    ws.onopen = () => {
      ws.send(JSON.stringify({ type: 'replay_start', hand_id: handId, speed: playbackSpeed }));
    };
    ws.onmessage = (event) => {
      const data = JSON.parse(event.data);
      switch (data.type) {
        case 'snapshot': setSnapshot(data.data); break;
        case 'action': setEvents((prev) => [...prev, data]); setCurrentEventIdx(data.seq); break;
        case 'hand_complete': setIsPlaying(false); setIsPaused(true); break;
      }
    };
    wsRef.current = ws;
    return () => { ws.close(); };
  }, [handId]);

  const togglePlay = () => {
    if (isPlaying) {
      wsRef.current?.send(JSON.stringify({ type: 'replay_control', command: 'pause' }));
      setIsPaused(true);
    } else {
      wsRef.current?.send(JSON.stringify({ type: 'replay_control', command: 'resume' }));
      setIsPaused(false);
    }
    setIsPlaying(!isPlaying);
  };

  const handleSpeedChange = (speed: number) => {
    setPlaybackSpeed(speed);
    wsRef.current?.send(JSON.stringify({ type: 'replay_control', command: 'speed', speed }));
  };

  const phaseLabel = (phase?: string) => {
    const map: Record<string, string> = { waiting: '等待', preflop: '翻前', flop: '翻牌圈', turn: '转牌圈', river: '河牌圈', showdown: '摊牌', handOver: '结束' };
    return map[phase || ''] || phase || '?';
  };

  return (
    <div className="replay-viewer">
      <div className="replay-header">
        <button className="btn-back" onClick={onClose}>← 返回</button>
        <h2>📹 手牌重放 #{handId}</h2>
        <span className="replay-phase">{snapshot && phaseLabel(snapshot.phase)}</span>
      </div>
      {snapshot && (
        <GameTable gameState={snapshot} myPlayerId={-1} mySeatIndex={null} isMyTurn={false} onAction={() => {}} onSeatClick={() => {}} isReplay={true} showAllCards={true} />
      )}
      {snapshot && (
        <div className="replay-controls">
          <div className="control-buttons">
            <button onClick={() => setCurrentTime(Math.max(0, currentTime - 10))} className="ctrl-btn">⏪</button>
            <button onClick={togglePlay} className="ctrl-btn">{isPaused ? '▶' : '⏸'}</button>
            <button onClick={() => setCurrentTime(Math.min(totalDuration, currentTime + 10))} className="ctrl-btn">⏩</button>
            {[1, 2, 4].map(s => (
              <button key={s} onClick={() => handleSpeedChange(s)} className={`ctrl-btn ${playbackSpeed === s ? 'active' : ''}`}>{s}×</button>
            ))}
          </div>
          <div className="event-timeline">
            <h4>📋 事件记录</h4>
            <div className="event-list">
              {events.map((evt, idx) => (
                <div key={idx} className={`event-item ${idx === currentEventIdx ? 'current' : ''}`}>
                  <span className="event-time">{evt.timestamp?.toFixed(1)}s</span>
                  <span className="event-type">{evt.type}</span>
                  <span className="event-detail">P{evt.player_id} {evt.details?.action} {evt.details?.amount}</span>
                </div>
              ))}
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default ReplayViewer;
