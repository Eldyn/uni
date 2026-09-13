/**
 * @file drawPile.ts
 * @brief Pure draw-pile stack math, split out of DrawPile3D.svelte so
 * baseBeats.ts can compute "exactly where the current top card visually sits"
 * without reimplementing DrawPile3D's own constants a second time, which
 * avoids the z-fight/thin-air bug.
 */

import type { BoardPlacement } from "./boardPlacement";

// Visual cap: rendering one plane per real card would draw 80+ overlapping
// meshes once the deck is full. The stack still reads as "a pile of cards" at
// any depth past a handful, so cap it and let depth communicate "still plenty
// left" rather than trying to render every card.
export const MAX_VISIBLE_STACK = 6;
// Same z-fighting fix as the other piles/hands: keep the step clear of
// CardMesh3D's own internal layer span (up to 0.004 world units).
export const DRAW_PILE_STACK_STEP = 0.02;
// Lifts the whole pile clear of the local hand's own height range.
export const PILE_BASE_HEIGHT = 0.6;
// Each deeper back peeks out a little above the one below it, toward the mat
// (-Z), so the pile reads as an actual stack instead of one lone back.
export const PILE_PEEK_Z = 0.02;

export function visibleDrawPileStackSize(rawSize: number): number {
	return Math.max(0, Math.min(MAX_VISIBLE_STACK, rawSize));
}

/**
 * The exact world position the pile's topmost visible card sits at, given the
 * PRE-draw visible stack size (i.e. before the card being animated has left)
 * — the departing card's flight has to start exactly where it visually was a
 * moment ago, not at the pile's base (y:0) or ignoring PILE_PEEK_Z, which is
 * what read as "z-fights the pile" and "appears from thin air".
 */
export function drawPileTopPose(
	placement: BoardPlacement,
	preDrawVisibleStackSize: number
): [number, number, number] {
	const topIndex = Math.max(0, visibleDrawPileStackSize(preDrawVisibleStackSize) - 1);
	return [
		placement.drawPileX,
		PILE_BASE_HEIGHT + topIndex * DRAW_PILE_STACK_STEP,
		placement.drawPileZ - topIndex * PILE_PEEK_Z * placement.drawPileScale
	];
}
