import React, { useMemo } from 'react';

const RANKS = ['A', 'K', 'Q', 'J', 'T', '9', '8', '7', '6', '5', '4', '3', '2'] as const;

interface Props {
  data: number[][];  // 13x13 equity values (0-1)
  title?: string;
}

const RangeHeatmap: React.FC<Props> = ({ data, title }) => {
  const maxVal = useMemo(() => {
    let m = 0;
    for (const row of data) for (const v of row) if (v > m) m = v;
    return m || 1;
  }, [data]);

  const cellLabels = useMemo(() => {
    return RANKS.map((r, row) =>
      RANKS.map((c, col) => {
        if (row === col) return r + c;
        if (row < col) return r + c + 's';
        return c + r + 'o';
      })
    );
  }, []);

  function equityColor(val: number): string {
    const t = val / maxVal;
    // Green (strong) → Yellow (medium) → Red (weak)
    if (t > 0.7) return `rgb(${Math.round(255 * (1 - t) * 2)}, ${Math.round(180 + 75 * t)}, 50)`;
    if (t > 0.4) return `rgb(${Math.round(200 + 55 * t)}, ${Math.round(180 * t)}, 30)`;
    return `rgb(${Math.round(180 + 75 * t)}, ${Math.round(60 * t)}, 20)`;
  }

  return (
    <div className="range-heatmap">
      {title && <h4>{title}</h4>}
      <div className="heatmap-grid">
        {cellLabels.map((row, ri) => (
          <div key={ri} className="heatmap-row">
            {row.map((label, ci) => {
              const val = data[ri]?.[ci] ?? 0;
              return (
                <div
                  key={ci}
                  className="heatmap-cell"
                  style={{ backgroundColor: equityColor(val) }}
                  title={`${label}: ${(val * 100).toFixed(1)}%`}
                >
                  <span className="heatmap-label">{label}</span>
                  <span className="heatmap-value">{(val * 100).toFixed(0)}%</span>
                </div>
              );
            })}
          </div>
        ))}
      </div>
    </div>
  );
};

export default RangeHeatmap;
