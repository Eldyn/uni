/**
 * @file units.ts
 * @brief Shared world-space constants for the 3D board, plus the conversion
 * factor from the "em" units the pure layout modules (handRing.ts,
 * discardPile.ts) were written in for the DOM board. A card is CARD_WIDTH
 * world units wide, matching --cardSize's baseline of 5em, so 1em maps to
 * EM_TO_WORLD world units.
 */

export const CARD_WIDTH = 1;
export const CARD_HEIGHT = CARD_WIDTH * 1.5357; // matches GameCard.svelte's --cardSize aspect ratio
export const EM_TO_WORLD = CARD_WIDTH / 5;

/** How far a lifted card rises. In a straight-down ortho view this doesn't move
 *  it on screen at all — it only decides which of two overlapping cards wins the
 *  depth test, which is exactly enough to reveal a covered one. */
export const CARD_HOVER_LIFT = 0.5;
/** Multiplier applied to a lifted card, so anything drawn around one (see
 *  CardMesh3D's `highlight` prop) can match its grown size. */
export const CARD_HOVER_SCALE = 1.2;

/** Minimum geometric stack step: precision floor for z-fighting prevention. */
export const MIN_STACK_STEP = 0.004;
export const RING_STACK_STEP = 0.02;


// Where the local hand row, the draw pile and their card size actually land is
// a function of the camera's frustum rather than a fixed world coordinate —
// see layout/boardPlacement.ts. This file stays pure card geometry.

/**
 * Computes whether a card's value layer should flip by 180° (Math.PI)
 * to remain upright and readable to the local viewer (e.g. keeping 6 vs 9 legible).
 * When alwaysUprightValues is enabled and the card's spin angle exceeds 90° from upright
 * (|normalizedSpinDeg| > 90°), returns Math.PI; otherwise returns 0.
 */
export function computeValueFlipRad(spinDeg: number, alwaysUprightValues: boolean): number {
	if (!alwaysUprightValues) return 0;
	const normalizedDeg = ((spinDeg % 360) + 540) % 360 - 180;
	return Math.abs(normalizedDeg) > 90 ? Math.PI : 0;
}

