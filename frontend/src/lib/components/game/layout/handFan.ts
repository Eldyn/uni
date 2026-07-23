/**
 * @file handFan.ts
 * @brief Pure per-card geometry for an opponent's turned-card mini fan. No
 * Svelte, no CSS, no DOM — every slot is a plain {x, y, rotateDeg} offset
 * from the fan's own center, so a future non-DOM renderer (e.g. Three.js)
 * can consume the same numbers directly instead of interpolated CSS strings.
 */

export interface CardFanSlot {
	/** Offset from the fan center, in em. */
	x: number;
	y: number;
	rotateDeg: number;
}

const FAN_ANGLE_STEP_DEG = 10; // rotation between adjacent cards at low card counts
const FAN_MAX_SPAN_DEG = 80; // total spread never exceeds this, however many cards
const FAN_RADIUS_EM = 7; // pivot distance behind the fan; larger = flatter curve

// The fan's own origin (0,0 pre-offset) is where the seat's avatar box also
// anchors from — push every card forward by this much so even the centermost
// card clears the avatar instead of sitting right on top of it.
const FAN_FORWARD_OFFSET_EM = 6.5;

/**
 * Returns one slot per card, arranged like a hand of cards spread from a
 * pivot behind the fan: cards curve away from center (and so away from the
 * avatar sitting at the fan's origin) instead of sitting in a straight row.
 */
export function computeHandFanSlots(cardCount: number): CardFanSlot[] {
	if (cardCount <= 0) return [];
	if (cardCount === 1) return [{ x: 0, y: FAN_FORWARD_OFFSET_EM, rotateDeg: 0 }];

	const step = Math.min(FAN_ANGLE_STEP_DEG, FAN_MAX_SPAN_DEG / (cardCount - 1));
	const center = (cardCount - 1) / 2;

	return Array.from({ length: cardCount }, (_, i) => {
		const rotateDeg = (i - center) * step;
		const angleRad = (rotateDeg * Math.PI) / 180;
		return {
			x: FAN_RADIUS_EM * Math.sin(angleRad),
			y: FAN_RADIUS_EM * (1 - Math.cos(angleRad)) + FAN_FORWARD_OFFSET_EM,
			rotateDeg
		};
	});
}
