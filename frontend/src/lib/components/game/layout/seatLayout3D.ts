/**
 * @file seatLayout3D.ts
 * @brief Pure world-space seat solver for the Threlte board. Projects the same
 * ring angles the DOM solver uses (seatLayout.ts's computeSeatAngles — single
 * source, so both boards seat opponents identically) onto a SUPERELLIPTIC
 * arch on the XZ playmat plane: wide and shallow in landscape so seats curve
 * over the playmat's far side with room to breathe, narrow and deep in
 * portrait so the rails run down the screen's long axis instead.
 *
 * Frame: origin = playmat center, +X = table-right, +Z = toward the local
 * player (camera side, south), −Z = far side. Angle 90 (due-top) therefore
 * maps to −Z, angle 0 (right) to +X, angle 180 (left) to −X.
 */

import {
	computeSeatAngles,
	CROSS_OPPONENT_COUNT,
	MAX_OPPONENTS,
	type ArcWarp,
	type ViewportInfo
} from "./seatLayout";

export interface SeatPosition3D {
	/** World position on the playmat plane; y is fixed by the renderer. */
	x: number;
	z: number;
	/** Y-rotation (radians) turning the seat's local +Z to face the center. */
	rotationY: number;
}

// Ring radii of the opponent ring, in world units. Landscape spreads the
// seats wide (X) while staying shallow (Z) so the board reads as an arch over
// the playmat and the whole grid stays short enough for large cards; portrait
// flips the proportions so the rails run down the sides instead. Landscape
// additionally widens toward RX + RX_MAX_EXTRA as the table fills, so a full
// arch uses the 16:9 slack instead of crowding the side seats inward.
export const LANDSCAPE_RING_RX = 6.2;
export const LANDSCAPE_RING_RX_MAX_EXTRA = 1.4;
export const LANDSCAPE_RING_RZ = 4.2;
// ...but only this far past the count-based spread. Filling the frustum's full
// width is what pinned a 4-player table's side seats to the screen edges while
// its top seat sat close over the felt: a 16:9 window's horizontal slack is
// slack the TABLE doesn't have, so spending all of it stretches three seats
// into a flat bar with two of them stranded a screen-width from the mat.
// A FULL table is the one case where that slack is real — the arch has to reach
// wide or it closes into a circle and wraps down past the local hand — so the
// allowance ramps with the same crowding ratio everything else here uses.
export const LANDSCAPE_RING_RX_FILL_SPARSE = 1.05;
export const LANDSCAPE_RING_RX_FILL_CROWDED = 1.3;
// Portrait's proportions track the screen's, not just "taller than wide": the
// frustum is a "contain" fit, so a ring whose width/depth ratio is flatter than
// the phone's own ratio makes the horizontal axis the binding one and buys that
// slack back as dead space above and below — which is exactly what made every
// card on a phone read at half the size it could have.
export const PORTRAIT_RING_RX = 3;
export const PORTRAIT_RING_RZ = 7.6;

// The deepest the landscape arch is allowed to grow when it fills vertical
// slack (see ringRadiiFor's frustumHalfHeight). Past this the arch stops
// reading as an arch over the mat and starts closing into a circle around it.
export const LANDSCAPE_RING_RZ_MAX = 7;

// A seat's ring cards + label extend roughly this far beyond the seat
// position itself (RING_RADIUS_EM, PlayerSeat3D.svelte's label offset, plus
// label text height/width) — without this margin a seat's name/ring sits
// right on the frustum edge and gets clipped. Shared by both axes: a
// left/right seat's label reaches outward in X the same way a top seat's
// reaches outward in Z. Portrait renders much smaller avatars and labels, so
// its reach is tighter — every unit of width the frustum doesn't need
// translates directly into bigger cards on a phone. Lives here (not
// designGrid.ts, which needs it) rather than the reverse, so ringRadiiFor can
// use it too without an import cycle.
export const OPPONENT_RING_REACH = 2.6;
// Scene3D shrinks the avatar and label toward a full table (crowdT -> 1) the
// same way it widens the ring — a reach margin sized for the biggest avatar
// stays the same width even once every seat is down to its smallest, which is
// dead space by the same "wasted margin" logic as the ring-fill above.
export const LANDSCAPE_RING_REACH_MIN = 1.6;
export const PORTRAIT_OPPONENT_RING_REACH = 1.15;

export function ringReachFor(viewport: ViewportInfo, opponentCount: number = MAX_OPPONENTS): number {
	if (viewport.orientation === "portrait") return PORTRAIT_OPPONENT_RING_REACH;
	const t = Math.min(
		1,
		Math.max(0, (opponentCount - CROSS_OPPONENT_COUNT) / (MAX_OPPONENTS - CROSS_OPPONENT_COUNT))
	);
	return OPPONENT_RING_REACH + (LANDSCAPE_RING_REACH_MIN - OPPONENT_RING_REACH) * t;
}

// The X coordinate follows a superellipse (|cos|^(2/exp)) rather than a plain
// ellipse: seats near the ring's bottom get pushed outward toward ±rx, so the
// shape reads as a reversed U — curved across the top, near-vertical sides —
// instead of side seats curling in toward the local hand. Z keeps the plain
// sine so the lowest seats still rise smoothly above the hand row. Portrait's
// higher exponent makes its rails hug the screen edges almost vertically.
export const LANDSCAPE_RING_X_EXPONENT = 2.5;
export const PORTRAIT_RING_X_EXPONENT = 5;

/**
 * @param frustumHalfWidth The camera frustum's actual half-width, in world
 * units. designGrid.ts fits the board to the viewport's aspect without
 * cropping either axis, so whichever axis isn't the binding one ends up with
 * slack — dead space beyond the outermost seat. Passing the real frustum here
 * lets the landscape ring grow to fill it (never shrinks below the count-based
 * spread), which is also what turns the arch into more of a flat "reverse U"
 * as it widens, exactly the shape a wide table has more of to give.
 * @param maxRz The deepest the arch may sit, i.e. the frustum's half-height
 * less whatever the caller knows a seat actually reaches past its own position
 * (handRing.ts's opponentSeatReachWorld). This is the same idea on the depth
 * axis and the reason a squarer window used to look wrong: the arch's depth was
 * a hard constant, so every extra row of vertical slack piled up as empty mat
 * ABOVE the top seat while that seat stayed pinned right on top of the center
 * pile. The arch only ever occupies -Z (see archPoint), so growing it pushes
 * seats away from the pile and toward the top edge without ever reaching down
 * into the hand row. It takes the measured reach rather than ringReachFor's
 * frustum-sizing estimate because this is the axis where the estimate being a
 * fifth of a card short shows up directly as a clipped seat.
 */
export function ringRadiiFor(
	viewport: ViewportInfo,
	opponentCount: number = MAX_OPPONENTS,
	frustumHalfWidth?: number,
	maxRz?: number
): { rx: number; rz: number } {
	if (viewport.orientation === "portrait") {
		return { rx: PORTRAIT_RING_RX, rz: PORTRAIT_RING_RZ };
	}
	const t = Math.min(
		1,
		Math.max(0, (opponentCount - CROSS_OPPONENT_COUNT) / (MAX_OPPONENTS - CROSS_OPPONENT_COUNT))
	);
	const rx = LANDSCAPE_RING_RX + LANDSCAPE_RING_RX_MAX_EXTRA * t;
	const maxFill =
		LANDSCAPE_RING_RX_FILL_SPARSE +
		(LANDSCAPE_RING_RX_FILL_CROWDED - LANDSCAPE_RING_RX_FILL_SPARSE) * t;
	const filledRx =
		frustumHalfWidth === undefined
			? rx
			: Math.min(
					rx * maxFill,
					Math.max(rx, frustumHalfWidth - ringReachFor(viewport, opponentCount))
				);
	const filledRz =
		maxRz === undefined
			? LANDSCAPE_RING_RZ
			: Math.min(LANDSCAPE_RING_RZ_MAX, Math.max(LANDSCAPE_RING_RZ, maxRz));
	return { rx: filledRx, rz: filledRz };
}

// Resolution of the arc-length table below. The arch's curvature is gentle, so
// a coarse table already resolves seat spacing far below one card width.
const ARC_SAMPLES = 96;

function archPoint(
	angleDeg: number,
	rx: number,
	rz: number,
	xExponent: number
): { x: number; z: number } {
	const angleRad = (angleDeg * Math.PI) / 180;
	const cos = Math.cos(angleRad);
	return {
		x: rx * Math.sign(cos) * Math.abs(cos) ** (2 / xExponent),
		z: -rz * Math.sin(angleRad)
	};
}

/**
 * Builds the warp that turns seatLayout.ts's even-in-ANGLE spread into an
 * even-in-DISTANCE one along this arch. The arch is far wider than it is deep
 * and its sides are near-vertical, so a degree of angle buys ~rz of travel at
 * the sides against ~rx at the top: without this, the side seats stack almost
 * on top of each other while the top ones spread out. Cached per arc since the
 * table only depends on the arc's endpoints and the ring's proportions.
 */
function arcLengthWarp(rx: number, rz: number, xExponent: number): ArcWarp {
	const tables = new Map<string, number[]>();

	return (t, startDeg, endDeg) => {
		const key = `${startDeg}:${endDeg}`;
		let cumulative = tables.get(key);
		if (!cumulative) {
			cumulative = [0];
			let previous = archPoint(startDeg, rx, rz, xExponent);
			for (let i = 1; i <= ARC_SAMPLES; i++) {
				const point = archPoint(
					startDeg + ((endDeg - startDeg) * i) / ARC_SAMPLES,
					rx,
					rz,
					xExponent
				);
				cumulative.push(cumulative[i - 1] + Math.hypot(point.x - previous.x, point.z - previous.z));
				previous = point;
			}
			tables.set(key, cumulative);
		}

		const total = cumulative[ARC_SAMPLES];
		if (total === 0) return t;

		// Invert the cumulative-length table: find the sample the target length
		// falls in, then interpolate within it.
		const target = t * total;
		let segment = 1;
		while (segment < ARC_SAMPLES && cumulative[segment] < target) segment++;
		const spanLength = cumulative[segment] - cumulative[segment - 1];
		const withinSegment = spanLength === 0 ? 0 : (target - cumulative[segment - 1]) / spanLength;
		return (segment - 1 + withinSegment) / ARC_SAMPLES;
	};
}

export function computeSeatPositions3D(
	opponentCount: number,
	viewport: ViewportInfo,
	frustumHalfWidth?: number,
	frustumHalfHeight?: number
): SeatPosition3D[] {
	const { rx, rz } = ringRadiiFor(viewport, opponentCount, frustumHalfWidth, frustumHalfHeight);
	const xExponent =
		viewport.orientation === "portrait" ? PORTRAIT_RING_X_EXPONENT : LANDSCAPE_RING_X_EXPONENT;
	const warp = arcLengthWarp(rx, rz, xExponent);
	return computeSeatAngles(opponentCount, viewport, warp).map((angleDeg) => {
		const { x, z } = archPoint(angleDeg, rx, rz, xExponent);
		// Rotating local +Z by rotationY yields (sin, cos); aim it at the origin
		// by matching the unit vector from the seat toward the center.
		const rotationY = Math.atan2(-x, -z);
		return { x, z, rotationY };
	});
}
