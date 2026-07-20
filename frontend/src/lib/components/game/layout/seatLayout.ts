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

const MIN_SCALE = 0.55;
const SCALE_DROPOFF_START = 4; // opponent counts at/below this render at full scale
const SCALE_DROPOFF_STEP = 0.06;

const RING_RX = 42; // % of field width
const RING_RY = 38; // % of field height
const TOP_ARC_HALF_WIDTH_DEG = 65; // seats within this arc of due-top read as "top" seats

const RAIL_MARGIN_PCT = 10;
const RAIL_TOP_PCT = 12;
const RAIL_BOTTOM_PCT = 78;

function scaleFor(opponentCount: number): number {
	if (opponentCount <= SCALE_DROPOFF_START) return 1;
	const over = opponentCount - SCALE_DROPOFF_START;
	return Math.max(MIN_SCALE, 1 - over * SCALE_DROPOFF_STEP);
}

/**
 * Given how many opponents need a seat and the current viewport, returns one
 * SeatPosition per opponent index (0 = the player immediately after the
 * local player in turn order, i.e. the seat that used to be hardcoded as
 * LAYOUT_RIGHT).
 *
 * Landscape: opponents fan across the top half of an ellipse (a "ring"),
 * which happens to match the old fixed RIGHT/TOP/LEFT feel at low counts.
 * Portrait: two vertical rails (left/right only, never top), matching the
 * mobile mockup; opponents alternate rail by index.
 */
export function computeSeatPositions(opponentCount: number, viewport: ViewportInfo): SeatPosition[] {
	if (opponentCount <= 0) return [];
	const scale = scaleFor(opponentCount);
	return viewport.orientation === "portrait"
		? computeRailSeats(opponentCount, scale)
		: computeRingSeats(opponentCount, scale);
}

function computeRingSeats(n: number, scale: number): SeatPosition[] {
	const seats: SeatPosition[] = [];
	for (let i = 0; i < n; i++) {
		const t = (i + 0.5) / n;
		const angleRad = t * Math.PI; // 0 = right, PI/2 = top, PI = left
		const angleDeg = (angleRad * 180) / Math.PI;
		const isTop = Math.abs(angleDeg - 90) <= TOP_ARC_HALF_WIDTH_DEG;

		seats.push({
			xPct: 50 + RING_RX * Math.cos(angleRad),
			yPct: 50 - RING_RY * Math.sin(angleRad),
			scale,
			handTransform: ringHandTransform(angleDeg, isTop),
			isTop,
			...ringOutwardPositions(angleDeg, isTop)
		});
	}
	return seats;
}

function ringHandTransform(angleDeg: number, isTop: boolean): string {
	if (isTop) return "translate(-50%, -75%) scaleY(-1)";
	const rotateDeg = angleDeg - 90; // 0deg (right) -> -90, 180deg (left) -> 90
	return `translate(-50%, -50%) rotate(${rotateDeg}deg)`;
}

function ringOutwardPositions(
	angleDeg: number,
	isTop: boolean
): Pick<SeatPosition, "labelPos" | "boxPos"> {
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

function computeRailSeats(n: number, scale: number): SeatPosition[] {
	const leftCount = Math.ceil(n / 2);
	const rightCount = Math.floor(n / 2);
	const seats: SeatPosition[] = [];

	for (let i = 0; i < n; i++) {
		const rail: "left" | "right" = i % 2 === 0 ? "left" : "right";
		const railIndex = Math.floor(i / 2);
		const railCount = rail === "left" ? leftCount : rightCount;
		const railT = railCount > 1 ? railIndex / (railCount - 1) : 0.5;

		seats.push({
			xPct: rail === "left" ? RAIL_MARGIN_PCT : 100 - RAIL_MARGIN_PCT,
			yPct: RAIL_TOP_PCT + railT * (RAIL_BOTTOM_PCT - RAIL_TOP_PCT),
			scale,
			handTransform:
				rail === "left"
					? "translate(-50%, -50%) rotate(90deg)"
					: "translate(-50%, -50%) rotate(-90deg)",
			labelPos: `top: 50%; ${rail}: -4.8em; transform: translateY(-50%);`,
			boxPos: `top: 50%; ${rail}: -3em; transform: translate(${rail === "left" ? "-50%" : "50%"}, -50%);`,
			isTop: false
		});
	}
	return seats;
}
