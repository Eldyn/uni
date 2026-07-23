/**
 * @file handRing.ts
 * @brief Pure per-card geometry for an OPPONENT's hand arranged as a ring
 * around the player, not a flat fan (see handFan.ts for the flat variant the
 * local hand still uses). No Svelte, no CSS, no DOM — every slot is a plain
 * {x, y, rotateDeg} offset from the ring's own center, so the Threlte renderer
 * consumes the numbers directly.
 *
 * Angle 0 points from the player TOWARD the playmat center (the "line to the
 * discard pile"): the first card always spawns on that line, and the renderer
 * adds the seat's own rotation so it stays aimed at the table. Cards step
 * around the circle by RING_STEP_DEG until a full 360deg is used, at which
 * point the step compresses to 360/cardCount so a big hand overlaps into a
 * complete ring instead of running past itself.
 */

export interface CardRingSlot {
	/** Offset from the ring center, in em. */
	x: number;
	y: number;
	rotateDeg: number;
}

const RING_STEP_DEG = 22; // angle between adjacent cards before the ring fills
const RING_RADIUS_EM = 4.5; // distance of each card from the avatar at the center

/**
 * Returns one slot per card, wrapped around a circle of radius RING_RADIUS_EM
 * centered on the avatar. Card i sits at angle i*step measured from the
 * center line (angle 0 = toward the playmat); once step*(cardCount) would
 * exceed a full turn the step tightens to 360/cardCount so the cards close
 * into an evenly overlapping ring.
 */
export function computeHandRingSlots(cardCount: number): CardRingSlot[] {
	if (cardCount <= 0) return [];
	if (cardCount === 1) return [{ x: 0, y: RING_RADIUS_EM, rotateDeg: 0 }];

	const step = Math.min(RING_STEP_DEG, 360 / cardCount);
	// Center the spread on the line to the playmat so the hand reads as a
	// symmetric arc at low counts and only closes into a full ring once it
	// wraps all the way around.
	const center = (cardCount - 1) / 2;

	return Array.from({ length: cardCount }, (_, i) => {
		const rotateDeg = (i - center) * step;
		const angleRad = (rotateDeg * Math.PI) / 180;
		return {
			x: RING_RADIUS_EM * Math.sin(angleRad),
			y: RING_RADIUS_EM * Math.cos(angleRad),
			rotateDeg
		};
	});
}
