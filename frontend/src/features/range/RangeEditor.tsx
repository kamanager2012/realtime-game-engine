import React, { useState, useCallback, useMemo } from 'react';

// 13x13 grid: pocket pairs on diagonal, suited above, offsuit below
const RANKS = ['A', 'K', 'Q', 'J', 'T', '9', '8', '7', '6', '5', '4', '3', '2'] as const;

type CellType = 'pair' | 'suited' | 'offsuit';

interface Cell {
  label: string;
  type: CellType;
  row: number;
  col: number;
}

interface Props {
  range: Set<string>;
  onChange: (range: Set<string>) => void;
  disabled?: boolean;
}

const CELLS: Cell[][] = RANKS.map((r, row) =>
  RANKS.map((c, col) => {
    let label: string;
    let type: CellType;
    if (row === col) {
      label = r + c;
      type = 'pair';
    } else if (row < col) {
      label = r + c + 's';
      type = 'suited';
    } else {
      label = c + r + 'o';
      type = 'offsuit';
    }
    return { label, type, row, col };
  })
);

function expandRange(rangeStr: string): Set<string> {
  const result = new Set<string>();
  const parts = rangeStr.split(',').map(s => s.trim()).filter(Boolean);
  for (const part of parts) {
    // Handle plus notation: "ATs+" means ATs,AKs
    if (part.endsWith('+')) {
      const base = part.slice(0, -1);
      const isSuited = base.endsWith('s');
      const isOffsuit = base.endsWith('o');
      const rankStr = isSuited || isOffsuit ? base.slice(0, -1) : base;
      if (rankStr.length === 2) {
        const hiIdx = RANKS.indexOf(rankStr[0] as any);
        const loIdx = RANKS.indexOf(rankStr[1] as any);
        if (hiIdx >= 0 && loIdx >= 0 && hiIdx !== loIdx) {
          const suffix = isSuited ? 's' : isOffsuit ? 'o' : '';
          for (let i = hiIdx; i < loIdx; i++) {
            const label = RANKS[hiIdx] + RANKS[i] + suffix;
            result.add(label);
          }
        }
      }
      if (base.length === 1 || (!isSuited && !isOffsuit && rankStr.length === 2 && rankStr[0] === rankStr[1])) {
        // Pair+: "QQ+" means QQ, KK, AA
        const pairRank = base.length === 1 ? base : rankStr[0] === rankStr[1] ? rankStr[0] : null;
        if (pairRank) {
          const idx = RANKS.indexOf(pairRank as any);
          if (idx >= 0) {
            for (let i = 0; i <= idx; i++) {
              result.add(RANKS[i] + RANKS[i]);
            }
          }
        }
      }
    } else {
      result.add(part);
    }
  }
  return result;
}

const RangeEditor: React.FC<Props> = ({ range, onChange, disabled }) => {
  const [inputValue, setInputValue] = useState('');
  const [dragState, setDragState] = useState<'none' | 'add' | 'remove'>('none');

  const handleCellInteraction = useCallback((label: string, mode: 'add' | 'remove') => {
    const next = new Set(range);
    if (mode === 'add') next.add(label);
    else next.delete(label);
    onChange(next);
  }, [range, onChange]);

  const handleMouseDown = useCallback((label: string) => {
    if (disabled) return;
    const mode = range.has(label) ? 'remove' : 'add';
    setDragState(mode);
    handleCellInteraction(label, mode);
  }, [range, disabled, handleCellInteraction]);

  const handleMouseEnter = useCallback((label: string) => {
    if (dragState === 'none' || disabled) return;
    handleCellInteraction(label, dragState);
  }, [dragState, disabled, handleCellInteraction]);

  const handleMouseUp = useCallback(() => {
    setDragState('none');
  }, []);

  const handleApplyInput = useCallback(() => {
    if (!inputValue.trim()) return;
    const expanded = expandRange(inputValue);
    const next = new Set(range);
    for (const hand of expanded) next.add(hand);
    onChange(next);
    setInputValue('');
  }, [inputValue, range, onChange]);

  const handleClear = useCallback(() => {
    onChange(new Set());
  }, [onChange]);

  const handleSelectAll = useCallback(() => {
    const all = new Set<string>();
    for (const row of CELLS) for (const cell of row) all.add(cell.label);
    onChange(all);
  }, [onChange]);

  const count = range.size;
  const totalCombos = useMemo(() => {
    let n = 0;
    for (const hand of range) {
      if (hand.length === 2) n += 6;       // pair
      else if (hand.endsWith('s')) n += 4;  // suited
      else if (hand.endsWith('o')) n += 12; // offsuit
    }
    return n;
  }, [range]);

  return (
    <div className="range-editor" onMouseUp={handleMouseUp} onMouseLeave={handleMouseUp}>
      <div className="range-header">
        <h3>范围编辑器</h3>
        <span className="range-stats">{count} 手牌 · {totalCombos} 组合 · {(totalCombos / 1326 * 100).toFixed(1)}%</span>
      </div>

      <div className="range-grid">
        {CELLS.map((row, ri) => (
          <div key={ri} className="range-row">
            {row.map((cell, ci) => {
              const active = range.has(cell.label);
              return (
                <button
                  key={ci}
                  className={`range-cell type-${cell.type} ${active ? 'active' : ''}`}
                  onMouseDown={() => handleMouseDown(cell.label)}
                  onMouseEnter={() => handleMouseEnter(cell.label)}
                  disabled={disabled}
                  title={cell.label}
                >
                  {cell.label}
                </button>
              );
            })}
          </div>
        ))}
      </div>

      <div className="range-input-row">
        <input
          type="text"
          value={inputValue}
          onChange={(e) => setInputValue(e.target.value)}
          onKeyDown={(e) => e.key === 'Enter' && handleApplyInput()}
          placeholder="ATs+, QQ+, 87s"
          className="range-input"
          disabled={disabled}
        />
        <button onClick={handleApplyInput} disabled={disabled}>添加</button>
        <button onClick={handleSelectAll} disabled={disabled}>全选</button>
        <button onClick={handleClear} disabled={disabled}>清空</button>
      </div>
    </div>
  );
};

export default RangeEditor;
export { expandRange };
