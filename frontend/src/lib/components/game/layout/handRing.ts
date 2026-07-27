/**
 * @file handRing.ts
 * @brief Pure per-card geometry for an OPPONENT's hand: one continuous formula
 * that scales smoothly from "a single card held out in front of the player,
 * facing the table" up through "a wide arc in front of them" to "a full closed
 * circle" as the hand grows — there is no branch on card count, just a step
 * angle (RING_STEP_DEG) between adjacent cards that saturates once the arc
 * would otherwise overshoot a full turn (see handLine.ts for the local
 * player's own hand, a straight overlapping row instead). No Svelte, no CSS,
 * no DOM — every slot is a plain {x, y, rotateDeg} offset from the ring's own
 * center, so the Threlte renderer consumes the numbers directly.
 *
 * Angle 0 points from the player TOWARD the playmat center (the "line to the
 * discard pile"): the arc is always centered on that line, so a single card
 * sits directly in front of the player facing the table, and a small hand
 * reads as a symmetric arc fanned out from that line. Once
 * step*(cardCount-1) would exceed a full turn, the step compresses to
 * 360/cardCount instead of continuing to grow the span past 360deg — closing
 * the arc into an evenly overlapping full ring at roughly 360/RING_STEP_DEG
 * cards (~20 at the default step).
 */

export interface CardRingSlot {
	/** Offset from the ring center, in em. */
	x: number;
	y: number;
	rotateDeg: number;
}

export const RING_RADIUS_EM = 5; // distance of each card from the avatar at the center
const RING_STEP_DEG = 18; // angle between adjacent cards while the hand still reads as an arc

/**
 * Returns one slot per card, centered on the line toward the playmat. Card i
 * sits at angle (i - center)*step, where step is RING_STEP_DEG clamped down
 * to 360/cardCount so the spread never exceeds a full turn — the same min()
 * naturally produces an arc for small hands and a closed ring for large ones.
 */
export function computeHandRingSlots(cardCount: number): CardRingSlot[] {
	if (cardCount <= 0) return [];
	if (cardCount === 1) return [{ x: 0, y: RING_RADIUS_EM, rotateDeg: 0 }];

	const step = Math.min(RING_STEP_DEG, 360 / cardCount);
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
