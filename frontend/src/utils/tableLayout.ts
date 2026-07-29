import { GamePhase } from '../types/game';

export const TABLE = {
  centerX: 400,
  centerY: 300,
  radiusX: 340,
  radiusY: 260,
  cardWidth: 48,
  cardHeight: 68,
  chipRadius: 18,
  avatarRadius: 24,
};

export function getSeatAngle(seatIndex: number, totalSeats: number): number {
  const startAngle = -Math.PI / 2;
  const angleStep = (2 * Math.PI) / totalSeats;
  return startAngle + seatIndex * angleStep;
}

export function getSeatPosition(
  seatIndex: number,
  totalSeats: number,
  cx: number = TABLE.centerX,
  cy: number = TABLE.centerY,
  rx: number = TABLE.radiusX,
  ry: number = TABLE.radiusY
): { x: number; y: number } {
  const angle = getSeatAngle(seatIndex, totalSeats);
  return {
    x: cx + rx * Math.cos(angle),
    y: cy + ry * Math.sin(angle),
  };
}

export function getCommunityCardPosition(
  index: number,
  totalCards: number
): { x: number; y: number } {
  const totalWidth = totalCards * (TABLE.cardWidth + 8);
  const startX = TABLE.centerX - totalWidth / 2 + index * (TABLE.cardWidth + 8);
  return { x: startX, y: TABLE.centerY - TABLE.cardHeight / 2 };
}

export function getPotPosition(cx: number = TABLE.centerX, cy: number = TABLE.centerY): { x: number; y: number } {
  return { x: cx, y: cy + TABLE.radiusY * 0.5 };
}

export function getPlayerLabelPosition(
  seatPos: { x: number; y: number },
  seatAngle: number
): { x: number; y: number; align: CanvasTextAlign } {
  const labelOffset = 52;
  const angle = seatAngle;

  let align: CanvasTextAlign = 'center';
  if (Math.cos(angle) > 0.3) align = 'left';
  else if (Math.cos(angle) < -0.3) align = 'right';

  return {
    x: seatPos.x + Math.cos(angle) * labelOffset,
    y: seatPos.y + Math.sin(angle) * labelOffset,
    align,
  };
}

export function getLocalSeatIndex(globalSeat: number, mySeat: number, totalSeats: number): number {
  return (globalSeat - mySeat + totalSeats) % totalSeats;
}

export function getPhaseName(phase: string): string {
  const map: Record<string, string> = {
    waiting: '等待开始',
    preflop: '翻前',
    flop: '翻牌圈',
    turn: '转牌圈',
    river: '河牌圈',
    showdown: '摊牌',
    handOver: '本手牌结束',
  };
  return map[phase] || phase;
}
