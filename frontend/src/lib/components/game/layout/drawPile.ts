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
// Minimum geometric stack step: precision floor for z-fighting prevention.
export const MIN_STACK_STEP = 0.004;
// Same z-fighting fix as the other piles/hands: keep the step clear of
// CardMesh3D's own internal layer span (up to 0.004 world units).
export const DRAW_PILE_STACK_STEP = 0.02;
export const DRAW_PILE_BURIED_DEPTH_CAP = 20;
// Lifts the whole pile clear of the local hand's own height range.
export const PILE_BASE_HEIGHT = 0.6;
// Each deeper back peeks out a little above the one below it, toward the mat
// (-Z), so the pile reads as an actual stack instead of one lone back.
export const PILE_PEEK_Z = 0.02;
export const MAX_PILE_HEIGHT = 1.2;

/**
 * Geometric stack depth (Y in world space / Z in depth buffer) for an item in
 * the draw pile. Cards buried deep beyond the DRAW_PILE_BURIED_DEPTH_CAP (20)
 * share a single bottom tier (height 0) so the pile's depth does not grow
 * unboundedly past 20 items.
 */
export function drawPileStackZ(index: number, totalCount?: number): number {
	if (totalCount === undefined) {
		return Math.min(index, DRAW_PILE_BURIED_DEPTH_CAP) * DRAW_PILE_STACK_STEP;
	}
	if (totalCount <= DRAW_PILE_BURIED_DEPTH_CAP) {
		return index * DRAW_PILE_STACK_STEP;
	}
	const buriedCount = totalCount - DRAW_PILE_BURIED_DEPTH_CAP;
	if (index < buriedCount) {
		return 0;
	}
	return (index - buriedCount + 1) * DRAW_PILE_STACK_STEP;
}

export function visibleDrawPileStackSize(rawSize: number): number {
	return Math.max(0, Math.min(MAX_VISIBLE_STACK, rawSize));
}

/**
 * Computes the number of rendered cards and vertical step between layers for
 * the draw pile based on the thickness mode setting.
 */
export function computeDrawPileCountAndStep(
	rawSize: number,
	mode: "full" | "capped"
): { renderedCount: number; stepY: number } {
	if (mode === "capped") {
		return {
			renderedCount: Math.max(0, Math.min(MAX_VISIBLE_STACK, rawSize)),
			stepY: DRAW_PILE_STACK_STEP
		};
	}

	if (rawSize <= 0) {
		return { renderedCount: 0, stepY: DRAW_PILE_STACK_STEP };
	}
	if (rawSize === 1) {
		return { renderedCount: 1, stepY: DRAW_PILE_STACK_STEP };
	}

	const stepY = Math.max(
		MIN_STACK_STEP,
		Math.min(DRAW_PILE_STACK_STEP, MAX_PILE_HEIGHT / (rawSize - 1))
	);
	const renderedCount = Math.min(
		rawSize,
		Math.floor(MAX_PILE_HEIGHT / MIN_STACK_STEP)
	);

	return { renderedCount, stepY };
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
	const visibleSize = visibleDrawPileStackSize(preDrawVisibleStackSize);
	const topIndex = Math.max(0, visibleSize - 1);
	return [
		placement.drawPileX,
		PILE_BASE_HEIGHT + drawPileStackZ(topIndex, visibleSize),
		placement.drawPileZ - topIndex * PILE_PEEK_Z * placement.drawPileScale
	];
}
