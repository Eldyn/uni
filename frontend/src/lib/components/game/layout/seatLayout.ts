/**
 * @file seatLayout.ts
 * @brief Pure seat-position solver for the game table. No Svelte, no DOM —
 * takes an opponent count + viewport and returns where each opponent's seat
 * goes, replacing GameBoard.svelte's old hardcoded LAYOUT_LEFT/TOP/RIGHT
 * constants and 1/2/3-length branching.
 */

export type Orientation = "landscape" | "portrait";

export interface ViewportInfo {
	width: number;
	height: number;
	orientation: Orientation;
}

export interface SeatPosition {
	/** Center point of the seat within the game field, in percent (0-100). */
	xPct: number;
	yPct: number;
	/** Uniform scale applied to the seat's avatar + card fan as count grows. */
	scale: number;
	/** CSS transform for the seat's card-back fan. */
	handTransform: string;
	/** Inline CSS positioning the name label outward from the seat. */
	labelPos: string;
	/** Inline CSS positioning the avatar box outward from the seat. */
	boxPos: string;
	/** Whether this seat reads as a "top" seat (label/box below the hand). */
	isTop: boolean;
}

const MIN_SCALE = 0.35;
const SCALE_DROPOFF_START = 3; // opponent counts at/below this render at full scale
const SCALE_DROPOFF_STEP = 0.09;

// Mobile opponent seats read as small avatar+fan icons lining the rails, not
// as readable hands (see the Ocho reference) — shrink well past the ring's
// own count-based scale-down.
const PORTRAIT_SCALE_MULTIPLIER = 0.45;

const TOP_ARC_HALF_WIDTH_DEG = 20; // seats within this arc of due-top read as "top" seats
const HAND_ROTATE_CLAMP_DEG = 90; // never flip a hand fan past vertical

// Landscape: opponents fan across the top half of a wide ellipse at low
// counts, widening toward wrapping most of the way around as the table
// fills up so a growing (and shrinking, see scaleFor) fan of cards never
// overflows onto a neighbouring seat.
const LANDSCAPE_RING_RX = 42; // % of field width
const LANDSCAPE_RING_RY = 38; // % of field height
const LANDSCAPE_BASE_HALF_SPAN_DEG = 90; // top half only, at/below CROSS_OPPONENT_COUNT

// Portrait: taller, narrower ellipse that already wraps further down the
// sides at low counts, matching the mobile mockup, and widens the same way.
const PORTRAIT_RING_RX = 40; // % of field width
const PORTRAIT_RING_RY = 44; // % of field height; capped at 50 so due-top stays on screen
const PORTRAIT_BASE_HALF_SPAN_DEG = 140;

// Ocho-style mobile rail: opponents past the 4-player cross never sit near
// due-top (that's HUD/timer territory) — they split into a left rail and a
// right rail instead, each hugging the corners and running down the sides.
// Must stay larger than TOP_ARC_HALF_WIDTH_DEG or a rail seat could still
// read as (and render like) a "top" seat.
const PORTRAIT_TOP_GAP_DEG = 30;

// Both orientations converge on the same near-full ring at the max table
// size, leaving only a gap at due-bottom for the local player's hand. Must
// stay below 180 so 90±MAX_HALF_SPAN_DEG never crosses due-bottom (270) or
// flips past due-top (-90) — either would swap which rail a seat's cos()
// sign puts it on.
const MAX_HALF_SPAN_DEG = 155;
const MAX_OPPONENTS = 14; // BOT_COUNT_MAX; also the practical opponent ceiling

function halfSpanFor(opponentCount: number, baseHalfSpan: number): number {
	if (opponentCount <= CROSS_OPPONENT_COUNT) return baseHalfSpan;
	const t = Math.min(1, (opponentCount - CROSS_OPPONENT_COUNT) / (MAX_OPPONENTS - CROSS_OPPONENT_COUNT));
	return baseHalfSpan + t * (MAX_HALF_SPAN_DEG - baseHalfSpan);
}

// With four players or fewer (up to 3 opponents), the local player anchors
// "bottom" and the opponents sit at exact compass points instead of an
// arc-offset approximation, reading as an "across the table" cross:
//   1 opponent  -> due-top
//   2 opponents -> due-right + due-left (across from each other)
//   3 opponents -> due-right + due-top + due-left
const CROSS_OPPONENT_COUNT = 3;
const CROSS_ANGLES_BY_COUNT: Record<number, number[]> = {
	1: [90],
	2: [0, 180],
	3: [0, 90, 180]
};

// Opponent mini-hands are turned-card icons, not a readable hand — even at
// the lowest player counts they should read smaller than the local player's
// own cards, never at full --cardSize.
const OPPONENT_SCALE_BASE = 0.6;

function scaleFor(opponentCount: number): number {
	if (opponentCount <= SCALE_DROPOFF_START) return OPPONENT_SCALE_BASE;
	const over = opponentCount - SCALE_DROPOFF_START;
	return Math.max(MIN_SCALE, OPPONENT_SCALE_BASE - over * SCALE_DROPOFF_STEP);
}

function arcAngles(n: number, startDeg: number, endDeg: number): number[] {
	return Array.from({ length: n }, (_, i) => startDeg + ((i + 0.5) / n) * (endDeg - startDeg));
}

// Splits n opponents across a right rail and a left rail, each running from
// just past the top gap out to the current half-span, so no seat is ever
// placed near due-top in portrait once there are more than 4 players.
function portraitRailAngles(n: number, halfSpan: number): number[] {
	const leftCount = Math.ceil(n / 2); // odd counts put the extra seat on the left rail
	const rightCount = n - leftCount;
	const rightAngles = arcAngles(rightCount, 90 - PORTRAIT_TOP_GAP_DEG, 90 - halfSpan);
	const leftAngles = arcAngles(leftCount, 90 + PORTRAIT_TOP_GAP_DEG, 90 + halfSpan);
	return [...rightAngles, ...leftAngles];
}

/**
 * Given how many opponents need a seat and the current viewport, returns one
 * SeatPosition per opponent index (0 = the player immediately after the
 * local player in turn order, i.e. the seat that used to be hardcoded as
 * LAYOUT_RIGHT).
 *
 * At exactly 3 opponents (4 players total), seats form a cross with the
 * local player: due-right, due-top, due-left. Beyond that, opponents fan
 * around an ellipse ("ring") sized per orientation — a wide top-half arc in
 * landscape, and in portrait a left rail + right rail (never due-top, per
 * the mobile mockup) that grow down toward the bottom corners.
 */
/**
 * The bare ring angles (in degrees, 90 = due-top / far side of the table) one
 * per opponent, before any DOM/world projection. Single-sources the cross vs.
 * arc vs. portrait-rail decision so seatLayout3D.ts places its world ring on
 * exactly the same seats as the DOM solver.
 */
export function computeSeatAngles(opponentCount: number, viewport: ViewportInfo): number[] {
	if (opponentCount <= 0) return [];
	const isPortrait = viewport.orientation === "portrait";
	const baseHalfSpan = isPortrait ? PORTRAIT_BASE_HALF_SPAN_DEG : LANDSCAPE_BASE_HALF_SPAN_DEG;
	const halfSpan = halfSpanFor(opponentCount, baseHalfSpan);

	if (opponentCount <= CROSS_OPPONENT_COUNT) return CROSS_ANGLES_BY_COUNT[opponentCount];
	return isPortrait
		? portraitRailAngles(opponentCount, halfSpan)
		: arcAngles(opponentCount, 90 - halfSpan, 90 + halfSpan);
}

export function computeSeatPositions(opponentCount: number, viewport: ViewportInfo): SeatPosition[] {
	if (opponentCount <= 0) return [];
	const isPortrait = viewport.orientation === "portrait";
	const scale = scaleFor(opponentCount) * (isPortrait ? PORTRAIT_SCALE_MULTIPLIER : 1);
	const rx = isPortrait ? PORTRAIT_RING_RX : LANDSCAPE_RING_RX;
	const ry = isPortrait ? PORTRAIT_RING_RY : LANDSCAPE_RING_RY;

	return computeSeatAngles(opponentCount, viewport).map((angleDeg) =>
		buildSeat(angleDeg, rx, ry, scale, isPortrait)
	);
}

function buildSeat(
	angleDeg: number,
	rx: number,
	ry: number,
	scale: number,
	isPortrait: boolean
): SeatPosition {
	const angleRad = (angleDeg * Math.PI) / 180;
	const isTop = Math.abs(angleDeg - 90) <= TOP_ARC_HALF_WIDTH_DEG;

	return {
		xPct: 50 + rx * Math.cos(angleRad),
		yPct: 50 - ry * Math.sin(angleRad),
		scale,
		handTransform: handTransform(angleDeg, isTop, isPortrait),
		isTop,
		...outwardPositions(angleDeg, isTop)
	};
}

function handTransform(angleDeg: number, isTop: boolean, isPortrait: boolean): string {
	if (isTop) return "translate(-50%, -75%) scaleY(-1)";
	// Mobile rails only ever run down the left or right edge (never near
	// top), so the hand stays fully vertical regardless of exactly where
	// along the rail a seat sits — a partial rotation would tilt cards
	// toward the HUD instead of reading as a clean left/right column.
	const rotateDeg = isPortrait
		? angleDeg < 90
			? -HAND_ROTATE_CLAMP_DEG
			: HAND_ROTATE_CLAMP_DEG
		: Math.max(-HAND_ROTATE_CLAMP_DEG, Math.min(HAND_ROTATE_CLAMP_DEG, angleDeg - 90));
	return `translate(-50%, -50%) rotate(${rotateDeg}deg)`;
}

function outwardPositions(angleDeg: number, isTop: boolean): Pick<SeatPosition, "labelPos" | "boxPos"> {
	if (isTop) {
		return {
			labelPos: "bottom: -3em; left: 50%; transform: translateX(-50%);",
			boxPos: "bottom: -1.2em; left: 50%; transform: translateX(-50%);"
		};
	}
	const onRight = angleDeg < 90;
	const side = onRight ? "right" : "left";
	return {
		labelPos: `top: 30%; ${side}: -4.8em; transform: translateY(-50%);`,
		boxPos: `top: 40%; ${side}: -3em; transform: translate(${onRight ? "50%" : "-50%"}, -50%);`
	};
}
