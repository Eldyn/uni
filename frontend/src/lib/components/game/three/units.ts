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
export const CARD_HOVER_SCALE = 1.08;

// Where the local hand row, the draw pile and their card size actually land is
// a function of the camera's frustum rather than a fixed world coordinate —
// see layout/boardPlacement.ts. This file stays pure card geometry.

