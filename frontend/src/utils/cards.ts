export const SUIT_NAMES = ['♠', '♥', '♦', '♣'];
export const RANK_NAMES = ['A', '2', '3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K'];
export const RANK_NAMES_SHORT = ['A', '2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K'];
export const SUIT_COLORS: Record<number, string> = { 0: 'black', 1: 'red', 2: 'red', 3: 'black' };


export function isValidCardId(encoded: number): boolean {
  return Number.isFinite(encoded) && encoded >= 0 && encoded < 52;
}

export function hasDealtHoleCards(holeCards?: number[] | null): boolean {
  return !!holeCards && holeCards.length === 2 && isValidCardId(holeCards[0]) && isValidCardId(holeCards[1]);
}

export function decodeCard(encoded: number): { suit: number; rank: number } {
  // Backend CardId: rank = id >> 2, suit = id & 3
  return {
    rank: encoded >> 2,
    suit: encoded & 3,
  };
}

export function cardToString(encoded: number): string {
  const { suit, rank } = decodeCard(encoded);
  return RANK_NAMES_SHORT[rank] + SUIT_NAMES[suit];
}

export const HAND_RANK_NAMES = [
  'High Card',
  'One Pair',
  'Two Pair',
  'Three of a Kind',
  'Straight',
  'Flush',
  'Full House',
  'Four of a Kind',
  'Straight Flush',
  'Royal Flush',
];

export function isRedSuit(suit: number): boolean {
  return suit === 1 || suit === 2;
}

export function getCardClassName(encoded: number): string {
  const { suit, rank } = decodeCard(encoded);
  const color = isRedSuit(suit) ? 'red' : 'black';
  return `card card-${color} rank-${rank} suit-${suit}`;
}
