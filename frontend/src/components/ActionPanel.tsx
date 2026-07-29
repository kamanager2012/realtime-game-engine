import React, { useState, useEffect, useRef } from 'react';
import { ActionType } from '../types/game';

interface Props {
  onSendAction: (action: ActionType, amount: number) => void;
  myChips: number;
  currentBet: number;
  myBet: number;
  minRaise: number;
  canCheck: boolean;
  canCall: boolean;
  callAmount: number;
  minBet: number;
  maxBet: number;
  actionEpoch: string;
}

const ActionPanel: React.FC<Props> = ({
  onSendAction,
  myChips,
  currentBet,
  myBet,
  minRaise,
  canCheck,
  canCall,
  callAmount,
  minBet,
  maxBet,
  actionEpoch,
}) => {
  const [sliderValue, setSliderValue] = useState(minRaise);
  const [selectedAction, setSelectedAction] = useState<string | null>(null);
  const [showSlider, setShowSlider] = useState(false);
  const sliderRef = useRef<HTMLInputElement>(null);

  useEffect(() => { setSliderValue(minRaise); }, [minRaise]);
  useEffect(() => {
    setSelectedAction(null);
    setShowSlider(false);
  }, [actionEpoch]);
  useEffect(() => { if (showSlider && sliderRef.current) sliderRef.current.focus(); }, [showSlider]);

  const handleActionClick = (action: string) => {
    switch (action) {
      case 'fold':
        onSendAction(ActionType.Fold, 0);
        setSelectedAction('fold');
        break;
      case 'check':
        onSendAction(ActionType.Check, 0);
        setSelectedAction('check');
        break;
      case 'call':
        onSendAction(ActionType.Call, Math.min(callAmount, myChips));
        setSelectedAction('call');
        break;
      case 'bet':
      case 'raise':
        setSelectedAction(action);
        setShowSlider(true);
        setSliderValue(Math.min(minRaise, myChips));
        break;
      case 'allin':
        onSendAction(ActionType.AllIn, myChips);
        setSelectedAction('allin');
        break;
    }
  };

  const handleSliderConfirm = () => {
    const amount = Math.round(sliderValue);
    if (selectedAction === 'raise') {
      onSendAction(ActionType.Raise, amount);
    } else {
      onSendAction(ActionType.Bet, amount);
    }
    setShowSlider(false);
    setSelectedAction(null);
  };

  const handleSliderCancel = () => {
    setShowSlider(false);
    setSelectedAction(null);
  };

  const toCall = currentBet - myBet;
  const effectiveCall = Math.min(toCall, myChips);
  const canBet = currentBet === 0 || currentBet === myBet;

  return (
    <div className="action-panel">
      <div className="chips-info">
        <span className="chips-label">💰 筹码: {myChips}</span>
        {toCall > 0 && <span className="to-call-label">需跟注: {effectiveCall}</span>}
      </div>

      {showSlider && (
        <div className="bet-slider-container">
          <div className="slider-header">
            <span>{selectedAction === 'raise' ? '加注金额' : '下注金额'}</span>
            <span className="slider-value">{Math.round(sliderValue)}</span>
          </div>
          <div className="slider-labels">
            <span>{minBet}</span>
            <span>{myChips}</span>
          </div>
          <input
            ref={sliderRef}
            type="range"
            className="bet-slider"
            min={minBet}
            max={myChips}
            value={sliderValue}
            onChange={(e) => setSliderValue(Number(e.target.value))}
          />
          <div className="slider-buttons">
            <button className="btn-cancel" onClick={handleSliderCancel}>取消</button>
            <button className="btn-confirm" onClick={handleSliderConfirm} disabled={sliderValue < minBet}>确认</button>
          </div>
        </div>
      )}

      <div className="action-buttons">
        <button className="action-btn btn-fold" onClick={() => handleActionClick('fold')} disabled={selectedAction != null}>
          <span className="btn-icon">💀</span><span className="btn-text">弃牌</span>
        </button>
        {canCheck && (
          <button className="action-btn btn-check" onClick={() => handleActionClick('check')} disabled={selectedAction != null}>
            <span className="btn-icon">✋</span><span className="btn-text">过牌</span>
          </button>
        )}
        {canCall && (
          <button className="action-btn btn-call" onClick={() => handleActionClick('call')} disabled={selectedAction != null}>
            <span className="btn-icon">🤝</span><span className="btn-text">跟注 {effectiveCall > 0 ? effectiveCall : ''}</span>
          </button>
        )}
        {canBet && !showSlider && (
          <button className="action-btn btn-bet" onClick={() => handleActionClick('bet')} disabled={selectedAction != null}>
            <span className="btn-icon">💰</span><span className="btn-text">下注</span>
          </button>
        )}
        {!canBet && !showSlider && (
          <button className="action-btn btn-raise" onClick={() => handleActionClick('raise')} disabled={selectedAction != null}>
            <span className="btn-icon">🔥</span><span className="btn-text">加注</span>
          </button>
        )}
        <button className="action-btn btn-allin" onClick={() => handleActionClick('allin')} disabled={selectedAction != null}>
          <span className="btn-icon">🚀</span><span className="btn-text">全押</span>
        </button>
      </div>
    </div>
  );
};

export default ActionPanel;
