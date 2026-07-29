import React, { useRef, useEffect, useCallback, useState } from 'react';
import { GameState, PlayerState, ActionType } from '../types/game';
import {
  TABLE,
  getSeatPosition,
  getPotPosition,
  getPlayerLabelPosition,
  getSeatAngle,
  getLocalSeatIndex,
  getPhaseName,
} from '../utils/tableLayout';
import { decodeCard, SUIT_NAMES, RANK_NAMES_SHORT, isRedSuit, hasDealtHoleCards } from '../utils/cards';

const CANVAS_WIDTH = 800;
const CANVAS_HEIGHT = 600;

function roundRect(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  w: number,
  h: number,
  r: number
) {
  const fn = (ctx as any).roundRect;
  if (fn) {
    fn.call(ctx, x, y, w, h, r);
  } else {
    ctx.rect(x, y, w, h);
  }
}

interface Props {
  gameState: GameState | null;
  myPlayerId: number;
  mySeatIndex: number | null;
  isMyTurn: boolean;
  onAction: (action: ActionType, amount: number) => void;
  onSeatClick: (seatIndex: number) => void;
  canSelectSeat?: boolean;
  pendingSeatIndex?: number | null;
  isSeating?: boolean;
  isReplay?: boolean;
  showAllCards?: boolean;
}

const GameTable: React.FC<Props> = ({
  gameState,
  myPlayerId,
  mySeatIndex,
  isMyTurn,
  onAction,
  onSeatClick,
  canSelectSeat = false,
  pendingSeatIndex = null,
  isSeating = false,
}) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [hoverSeat, setHoverSeat] = useState<number | null>(null);

  const drawCard = useCallback((ctx: CanvasRenderingContext2D, encodedCard: number, x: number, y: number) => {
    const w = TABLE.cardWidth;
    const h = TABLE.cardHeight;

    ctx.fillStyle = '#fff';
    ctx.beginPath();
    roundRect(ctx, x, y, w, h, 4);
    ctx.fill();
    ctx.strokeStyle = '#ddd';
    ctx.lineWidth = 1;
    ctx.stroke();

    const { suit, rank } = decodeCard(encodedCard);
    const isRed = isRedSuit(suit);

    ctx.fillStyle = isRed ? '#cc0000' : '#000';
    ctx.font = 'bold 16px Arial';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(RANK_NAMES_SHORT[rank], x + 10, y + 14);
    ctx.font = '12px Arial';
    ctx.fillText(SUIT_NAMES[suit], x + 10, y + 28);
    ctx.font = `${Math.floor(w * 0.4)}px Arial`;
    ctx.fillText(SUIT_NAMES[suit], x + w / 2, y + h / 2 + 2);
  }, []);

  const drawCardBack = useCallback((ctx: CanvasRenderingContext2D, x: number, y: number) => {
    const w = TABLE.cardWidth;
    const h = TABLE.cardHeight;

    const grad = ctx.createLinearGradient(x, y, x + w, y + h);
    grad.addColorStop(0, '#1a3a6b');
    grad.addColorStop(1, '#2a5a9b');
    ctx.fillStyle = grad;
    ctx.beginPath();
    roundRect(ctx, x, y, w, h, 4);
    ctx.fill();

    ctx.strokeStyle = 'rgba(255,255,255,0.15)';
    ctx.lineWidth = 1;
    for (let i = 0; i < 3; i++) {
      const cx2 = x + w / 2;
      const cy2 = y + h / 2;
      const size = 10 + i * 8;
      ctx.beginPath();
      ctx.moveTo(cx2, cy2 - size);
      ctx.lineTo(cx2 + size, cy2);
      ctx.lineTo(cx2, cy2 + size);
      ctx.lineTo(cx2 - size, cy2);
      ctx.closePath();
      ctx.stroke();
    }
  }, []);

  const drawDealerButton = useCallback((ctx: CanvasRenderingContext2D, pos: { x: number; y: number }, angle: number) => {
    const btnRadius = 10;
    const offset = TABLE.avatarRadius + 16;
    const bx = pos.x + Math.cos(angle) * offset;
    const by = pos.y + Math.sin(angle) * offset;

    ctx.beginPath();
    ctx.arc(bx, by, btnRadius, 0, 2 * Math.PI);
    ctx.fillStyle = '#ffd700';
    ctx.fill();
    ctx.strokeStyle = '#b8860b';
    ctx.lineWidth = 2;
    ctx.stroke();

    ctx.fillStyle = '#000';
    ctx.font = 'bold 10px Arial';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('D', bx, by + 1);
  }, []);

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const W = canvas.width;
    const H = canvas.height;

    ctx.clearRect(0, 0, W, H);

    ctx.fillStyle = '#1a6b3c';
    ctx.fillRect(0, 0, W, H);

    ctx.fillStyle = 'rgba(0,0,0,0.03)';
    for (let i = 0; i < W; i += 4) {
      for (let j = 0; j < H; j += 4) {
        ctx.fillRect(i, j, 2, 2);
      }
    }

    ctx.beginPath();
    ctx.ellipse(W / 2, H / 2, TABLE.radiusX + 20, TABLE.radiusY + 20, 0, 0, 2 * Math.PI);
    ctx.fillStyle = '#2d8a5c';
    ctx.fill();
    ctx.strokeStyle = '#4aab6e';
    ctx.lineWidth = 3;
    ctx.stroke();

    ctx.beginPath();
    ctx.ellipse(W / 2, H / 2, TABLE.radiusX - 15, TABLE.radiusY - 15, 0, 0, 2 * Math.PI);
    ctx.strokeStyle = 'rgba(255,255,255,0.1)';
    ctx.lineWidth = 2;
    ctx.stroke();

    if (!gameState) {
      ctx.fillStyle = 'rgba(255,255,255,0.5)';
      ctx.font = '24px Arial';
      ctx.textAlign = 'center';
      ctx.fillText('Waiting for players...', W / 2, H / 2);
      return;
    }

    const myGlobalSeat = mySeatIndex ?? -1;
    const avatarColors = ['#e74c3c', '#3498db', '#2ecc71', '#f39c12', '#9b59b6', '#1abc9c'];

    gameState.players.forEach((player, seatIdx) => {
      if (!player.occupied) {
        const pos = getSeatPosition(seatIdx, 6);
        const isHover = canSelectSeat && !isSeating && hoverSeat === seatIdx;
        const isPending = pendingSeatIndex === seatIdx;
        ctx.beginPath();
        ctx.arc(pos.x, pos.y, TABLE.avatarRadius, 0, 2 * Math.PI);
        ctx.fillStyle = isPending ? 'rgba(255,215,0,0.35)' : isHover ? 'rgba(255,215,0,0.25)' : 'rgba(255,255,255,0.05)';
        ctx.fill();
        ctx.strokeStyle = isPending ? '#ffd700' : isHover ? '#ffd700' : 'rgba(255,255,255,0.15)';
        ctx.lineWidth = 1;
        ctx.setLineDash([4, 4]);
        ctx.stroke();
        ctx.setLineDash([]);
        ctx.fillStyle = isHover ? '#ffd700' : 'rgba(255,255,255,0.3)';
        ctx.font = '11px Arial';
        ctx.textAlign = 'center';
        ctx.fillText(isPending ? '...' : '坐下', pos.x, pos.y + 4);
        return;
      }

      const isSelf = myPlayerId > 0 && player.player_id === myPlayerId;
      const angle = getSeatAngle(seatIdx, 6);
      const seatPos = getSeatPosition(seatIdx, 6);
      const isCurrentPlayer = player.player_id === gameState.current_player_id;
      const isMyTurnFlag = isCurrentPlayer && isSelf;

      ctx.beginPath();
      ctx.arc(seatPos.x, seatPos.y, TABLE.avatarRadius + 8, 0, 2 * Math.PI);
      if (isMyTurnFlag) {
        ctx.fillStyle = '#ffd700';
        ctx.shadowColor = '#ffd700';
        ctx.shadowBlur = 15;
      } else if (isSelf) {
        ctx.fillStyle = '#4a9eff';
      } else if (player.status === 'folded') {
        ctx.fillStyle = 'rgba(128,128,128,0.5)';
      } else {
        ctx.fillStyle = 'rgba(0,0,0,0.4)';
      }
      ctx.fill();
      ctx.shadowBlur = 0;

      ctx.beginPath();
      ctx.arc(seatPos.x, seatPos.y, TABLE.avatarRadius + 8, 0, 2 * Math.PI);
      ctx.strokeStyle = isMyTurnFlag ? '#fff' : 'rgba(255,255,255,0.3)';
      ctx.lineWidth = 2;
      ctx.stroke();

      ctx.save();
      ctx.beginPath();
      ctx.arc(seatPos.x, seatPos.y, TABLE.avatarRadius, 0, 2 * Math.PI);
      ctx.clip();
      ctx.fillStyle = player.status === 'folded' ? '#555' : avatarColors[seatIdx % avatarColors.length];
      ctx.fillRect(seatPos.x - TABLE.avatarRadius, seatPos.y - TABLE.avatarRadius, TABLE.avatarRadius * 2, TABLE.avatarRadius * 2);
      ctx.fillStyle = '#fff';
      ctx.font = `bold ${TABLE.avatarRadius}px Arial`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      const initial = player.display_name ? player.display_name[0].toUpperCase() : '?';
      ctx.fillText(initial, seatPos.x, seatPos.y);
      ctx.restore();

      const labelPos = getPlayerLabelPosition(seatPos, angle);
      ctx.fillStyle = isSelf ? '#fff' : 'rgba(255,255,255,0.7)';
      ctx.font = `${isSelf ? '14' : '12'}px monospace`;
      ctx.textAlign = labelPos.align;
      ctx.fillText(`${player.display_name || `P${player.player_id}`, 20}`, labelPos.x, labelPos.y - 10);
      ctx.fillText(`💰 ${player.chips}`, labelPos.x, labelPos.y + 6);

      if (player.bet_this_round > 0) {
        ctx.fillStyle = '#ffd700';
        ctx.font = '11px monospace';
        ctx.fillText(`Bet: ${player.bet_this_round}`, labelPos.x, labelPos.y + 20);
      }

      if (player.status === 'folded') {
        ctx.fillStyle = 'rgba(255,100,100,0.8)';
        ctx.font = '11px Arial';
        ctx.fillText('FOLD', labelPos.x, labelPos.y + 22);
      } else if (player.status === 'all_in') {
        ctx.fillStyle = '#ff4444';
        ctx.font = 'bold 11px Arial';
        ctx.fillText('ALL IN!', labelPos.x, labelPos.y + 22);
      }

      if (isSelf && hasDealtHoleCards(player.hole_cards)) {
        const cardY = seatPos.y + TABLE.avatarRadius + 25;
        drawCard(ctx, player.hole_cards[0], seatPos.x - TABLE.cardWidth / 2 - 2, cardY);
        drawCard(ctx, player.hole_cards[1], seatPos.x + TABLE.cardWidth / 2 + 2, cardY);
      } else if (player.status !== 'folded' && hasDealtHoleCards(player.hole_cards)) {
        const cardY = seatPos.y + (Math.sin(angle) > 0 ? TABLE.avatarRadius + 20 : -TABLE.avatarRadius - 35);
        drawCardBack(ctx, seatPos.x - TABLE.cardWidth / 2 - 2, cardY);
        drawCardBack(ctx, seatPos.x + TABLE.cardWidth / 2 + 2, cardY);
      }

      if (seatIdx === gameState.dealer_seat) {
        drawDealerButton(ctx, seatPos, angle);
      }

      if (gameState.winners?.includes(player.player_id)) {
        ctx.fillStyle = 'rgba(255,215,0,0.3)';
        ctx.beginPath();
        ctx.arc(seatPos.x, seatPos.y, TABLE.avatarRadius + 12, 0, 2 * Math.PI);
        ctx.fill();
        ctx.fillStyle = '#ffd700';
        ctx.font = 'bold 18px Arial';
        ctx.textAlign = 'center';
        ctx.fillText('🏆', seatPos.x, seatPos.y - TABLE.avatarRadius - 18);
      }
    });


    const heroPlayer = myPlayerId > 0 ? gameState.players.find((p) => p.player_id === myPlayerId) : undefined;
    if (heroPlayer && hasDealtHoleCards(heroPlayer.hole_cards)) {
      const heroY = H - TABLE.cardHeight - 28;
      const heroX = W / 2;
      ctx.fillStyle = 'rgba(0,0,0,0.55)';
      ctx.beginPath();
      roundRect(ctx, heroX - 90, heroY - 18, 180, TABLE.cardHeight + 36, 10);
      ctx.fill();
      ctx.strokeStyle = '#ffd700';
      ctx.lineWidth = 2;
      ctx.stroke();
      ctx.fillStyle = '#ffd700';
      ctx.font = 'bold 13px Arial';
      ctx.textAlign = 'center';
      ctx.fillText('你的手牌', heroX, heroY - 4);
      drawCard(ctx, heroPlayer.hole_cards[0], heroX - TABLE.cardWidth - 6, heroY + 8);
      drawCard(ctx, heroPlayer.hole_cards[1], heroX + 6, heroY + 8);
    }

    const community = gameState.community_cards || [];
    const canShowCards = community.length > 0 && gameState.phase !== 'preflop' && gameState.phase !== 'waiting';

    if (canShowCards) {
      const cx = W / 2;
      const cy = H / 2;
      const maxSlots = 5;
      const totalWidth = maxSlots * (TABLE.cardWidth + 8);
      const startX = cx - totalWidth / 2;

      ctx.fillStyle = '#ffd700';
      ctx.font = 'bold 12px Arial';
      ctx.textAlign = 'center';
      ctx.fillText('公共牌', cx, cy - TABLE.cardHeight / 2 - 10);

      for (let i = 0; i < maxSlots; i++) {
        const x = startX + i * (TABLE.cardWidth + 8);
        const y = cy - TABLE.cardHeight / 2;
        if (i < community.length) {
          drawCard(ctx, community[i], x, y);
        } else {
          drawCardBack(ctx, x, y);
        }
      }
    }

    const potPos = getPotPosition(W / 2, H / 2);
    ctx.fillStyle = 'rgba(0,0,0,0.5)';
    ctx.beginPath();
    roundRect(ctx, potPos.x - 70, potPos.y - 25, 140, 35, 8);
    ctx.fill();
    ctx.fillStyle = '#ffd700';
    ctx.font = 'bold 16px monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(`💰 底池: ${Math.round(gameState.pot)}`, potPos.x, potPos.y - 3);

    ctx.fillStyle = 'rgba(255,150,150,0.8)';
    ctx.font = '14px Arial';
    ctx.textAlign = 'center';
    ctx.fillText(`[${getPhaseName(gameState.phase)}]`, potPos.x, potPos.y + 25);

    if (gameState.side_pots?.length > 0) {
      gameState.side_pots.forEach((sidePot, idx) => {
        ctx.fillStyle = 'rgba(255,200,0,0.7)';
        ctx.font = '12px monospace';
        ctx.fillText(`边池 ${idx + 1}: ${sidePot.amount}`, potPos.x, potPos.y + 50 + idx * 20);
      });
    }

    if (isMyTurn) {
      ctx.fillStyle = 'rgba(255,215,0,0.15)';
      ctx.beginPath();
      ctx.arc(W / 2, H / 2, TABLE.radiusX - 30, 0, 2 * Math.PI);
      ctx.fill();
      ctx.fillStyle = '#ffd700';
      ctx.font = 'bold 16px Arial';
      ctx.textAlign = 'center';
      ctx.fillText('🎯 轮到你了！', W / 2, H / 2 + TABLE.radiusY + 30);
    }
  }, [gameState, myPlayerId, mySeatIndex, isMyTurn, canSelectSeat, isSeating, pendingSeatIndex, hoverSeat, drawCard, drawCardBack, drawDealerButton]);

  useEffect(() => { draw(); }, [draw]);

  const findSeatAt = useCallback((clientX: number, clientY: number): number | null => {
    const rect = canvasRef.current?.getBoundingClientRect();
    if (!rect || !gameState) return null;
    const scaleX = CANVAS_WIDTH / rect.width;
    const scaleY = CANVAS_HEIGHT / rect.height;
    const clickX = (clientX - rect.left) * scaleX;
    const clickY = (clientY - rect.top) * scaleY;

    const hitRadius = TABLE.avatarRadius + 30;
    for (let seatIdx = 0; seatIdx < 6; seatIdx++) {
      const player = gameState.players[seatIdx];
      if (player.occupied) continue;
      const pos = getSeatPosition(seatIdx, 6);
      const dist = Math.sqrt((clickX - pos.x) ** 2 + (clickY - pos.y) ** 2);
      if (dist < hitRadius) return seatIdx;
    }
    return null;
  }, [gameState]);

  const handleClick = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    if (!canSelectSeat || isSeating) return;
    const seatIdx = findSeatAt(e.clientX, e.clientY);
    if (seatIdx !== null) onSeatClick(seatIdx);
  }, [canSelectSeat, isSeating, findSeatAt, onSeatClick]);

  const handleMouseMove = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    if (!canSelectSeat) {
      setHoverSeat(null);
      return;
    }
    setHoverSeat(findSeatAt(e.clientX, e.clientY));
  }, [canSelectSeat, findSeatAt]);

  return (
    <canvas
      ref={canvasRef}
      width={CANVAS_WIDTH}
      height={CANVAS_HEIGHT}
      onClick={handleClick}
      onMouseMove={handleMouseMove}
      onMouseLeave={() => setHoverSeat(null)}
      style={{
        borderRadius: 12,
        cursor: canSelectSeat && !isSeating && hoverSeat !== null ? 'pointer' : 'default',
        maxWidth: '100%',
        height: 'auto',
        boxShadow: '0 8px 32px rgba(0,0,0,0.4)',
      }}
    />
  );
};

export default GameTable;
