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

import { CARD_HEIGHT, CARD_WIDTH } from "../three/units";

export interface CardRingSlot {
	/** Offset from the ring center, in em. */
	x: number;
	y: number;
	rotateDeg: number;
}

export const RING_RADIUS_EM = 5; // distance of each card from the avatar at the center
const RING_STEP_DEG = 18; // angle between adjacent cards while the hand still reads as an arc

export const RING_STACK_STEP = 0.02;

// Gap left between the avatar's own edge and the nearest ring card's inner
// edge — enough to read as a ring around the icon rather than cards grazing it.
export const RING_CLEARANCE_WORLD = 0.22;

/**
 * World-space radius the ring of cards has to sit at for a seat drawing its
 * avatar at `avatarWorldSize` across and its cards at `cardScale`. A ring card's
 * near edge sits half a card's height inside its own center, so the center has
 * to sit that much further out again for the edge to actually clear the avatar.
 */
export function opponentRingRadiusWorld(avatarWorldSize: number, cardScale: number): number {
	return avatarWorldSize / 2 + (CARD_HEIGHT * cardScale) / 2 + RING_CLEARANCE_WORLD;
}

/**
 * The uniform scale PlayerSeat3D multiplies `computeHandRingSlots`' unit
 * directions by so a seat's ring lands at `opponentRingRadiusWorld` for the
 * avatar and card size it is drawn with. Slots are authored at RING_RADIUS_EM,
 * so dividing the required world radius back out by that constant repoints
 * them at the radius the avatar actually needs.
 *
 * The scale depends only on the avatar and card sizes — a seat's position and
 * rotation place an already-scaled ring, they don't resize it.
 */
export function ringRadialScale(avatarWorld: number, cardScale: number): number {
	return opponentRingRadiusWorld(avatarWorld, cardScale) / RING_RADIUS_EM;
}

/**
 * How far a seat's drawn cards reach from the seat's own position — the ring
 * radius plus the card sitting on it, measured to its CORNER rather than its
 * edge. Ring cards are spun to stay radial (PlayerSeat3D), and a wide fan turns
 * some of them far enough that a corner, not an edge, is the outermost point;
 * measuring to the edge understates the reach by about a fifth of a card, which
 * is exactly the sliver that ends up clipped off the top of the screen.
 */
export function opponentSeatReachWorld(avatarWorldSize: number, cardScale: number): number {
	const cardHalfDiagonal = (Math.hypot(CARD_WIDTH, CARD_HEIGHT) * cardScale) / 2;
	return opponentRingRadiusWorld(avatarWorldSize, cardScale) + cardHalfDiagonal;
}

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

/**
 * Converts one ring slot from PlayerSeat3D's own local, seat-rotated frame
 * into a world-absolute [x, y, z]. Must match PlayerSeat3D.svelte's own
 * `<T.Group position.x={seat.x} position.z={seat.z} rotation.y={seat.rotationY}>`
 * wrapping exactly: local (slot.x*radialScale, slot.y*radialScale) rotates by
 * rotationY (Three's standard Y-axis rotation: worldX = lx*cos+lz*sin,
 * worldZ = -lx*sin+lz*cos, where local "depth" lz is slot.y), then translates
 * by the seat's own world position. `index` reproduces the same
 * `i * RING_STACK_STEP` Y-stacking PlayerSeat3D's own render uses.
 */
export function ringSlotWorldPose(
	seat: { x: number; z: number; rotationY: number },
	slot: CardRingSlot,
	index: number,
	radialScale: number,
	ringStackStep = RING_STACK_STEP
): [number, number, number] {
	const lx = slot.x * radialScale;
	const lz = slot.y * radialScale;
	const cos = Math.cos(seat.rotationY);
	const sin = Math.sin(seat.rotationY);
	const worldX = seat.x + (lx * cos + lz * sin);
	const worldZ = seat.z + (-lx * sin + lz * cos);
	return [worldX, index * ringStackStep, worldZ];
}

/**
 * Position "in front" of an opponent seat along the line toward the playmat center / discard pile,
 * spaced cleanly beyond the outer edge of their hand card arc.
 */
export function opponentFrontWorldPose(
	seat: { x: number; z: number; rotationY: number },
	avatarWorldSize: number,
	cardScale: number
): { position: [number, number, number]; spinDeg: number } {
	const ringRadiusWorld = opponentRingRadiusWorld(avatarWorldSize, cardScale);
	const frontOffsetWorld = ringRadiusWorld + (CARD_HEIGHT * cardScale) / 2 + 0.6;
	const sin = Math.sin(seat.rotationY);
	const cos = Math.cos(seat.rotationY);
	const worldX = seat.x + frontOffsetWorld * sin;
	const worldZ = seat.z + frontOffsetWorld * cos;
	const spinDeg = (seat.rotationY * 180) / Math.PI + 180;
	return {
		position: [worldX, 0.05, worldZ],
		spinDeg
	};
}
