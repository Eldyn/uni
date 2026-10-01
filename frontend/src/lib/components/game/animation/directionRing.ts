/**
 * @file directionRing.ts
 * @brief Pure helpers for the table's play-direction ring: which way it
 * circulates and whether a turn advance reversed it.
 */

export type DirectionSign = 1 | -1;

export const RING_FLIP_SECONDS = 0.35;
export const RING_SPIN_RADIANS_PER_SECOND = 0.25;
export const RING_IDLE_OPACITY = 0.22;
export const RING_FLIP_PEAK_OPACITY = 0.7;

/** Positive play directions (1) step to the next seat, due right of the
 *  local player, so they run counter-clockwise on screen; others are -1. */
export function directionSign(playDirection: number): DirectionSign {
	return playDirection > 0 ? 1 : -1;
}

export function detectReverse(previous: number, next: number): boolean {
	return directionSign(previous) !== directionSign(next);
}
