/**
 * @file directionRing.ts
 * @brief Pure helpers for the table's play direction: which way it runs and
 * whether a turn advance reversed it.
 */

export type DirectionSign = 1 | -1;

/** Positive play directions (1) step to the next seat, due right of the
 *  local player, so they run counter-clockwise on screen; others are -1. */
export function directionSign(playDirection: number): DirectionSign {
	return playDirection > 0 ? 1 : -1;
}

export function detectReverse(previous: number, next: number): boolean {
	return directionSign(previous) !== directionSign(next);
}
