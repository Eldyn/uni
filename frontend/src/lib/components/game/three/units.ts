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
