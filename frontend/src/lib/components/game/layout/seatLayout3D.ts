/**
 * @file seatLayout3D.ts
 * @brief Pure world-space seat solver for the Threlte board. Projects the same
 * ring angles the DOM solver uses (seatLayout.ts's computeSeatAngles — single
 * source, so both boards seat opponents identically) onto a CIRCULAR ring on
 * the XZ playmat plane. Unlike the DOM ellipse this ring is round (equal half
 * width/depth, per the reference art); portrait framing is handled camera-side
 * (cameraRig), not by squashing the ring here.
 *
 * Frame: origin = playmat center, +X = table-right, +Z = toward the local
 * player (camera side, south), −Z = far side. Angle 90 (due-top) therefore
 * maps to −Z, angle 0 (right) to +X, angle 180 (left) to −X.
 */

import { computeSeatAngles, type ViewportInfo } from "./seatLayout";

export interface SeatPosition3D {
	/** World position on the playmat plane; y is fixed by the renderer. */
	x: number;
	z: number;
	/** Y-rotation (radians) turning the seat's local +Z to face the center. */
	rotationY: number;
}

// World radius of the opponent ring. Equal in X and Z — a true circle, not the
// DOM ellipse. Chosen so a full ring of card-back rings clears the central
// discard/draw piles without overlapping them.
export const SEAT_RING_RADIUS = 6;

export function computeSeatPositions3D(
	opponentCount: number,
	viewport: ViewportInfo
): SeatPosition3D[] {
	return computeSeatAngles(opponentCount, viewport).map((angleDeg) => {
		const angleRad = (angleDeg * Math.PI) / 180;
		const x = SEAT_RING_RADIUS * Math.cos(angleRad);
		const z = -SEAT_RING_RADIUS * Math.sin(angleRad);
		// Rotating local +Z by rotationY yields (sin, cos); aim it at the origin
		// by matching the unit vector from the seat toward the center.
		const rotationY = Math.atan2(-x, -z);
		return { x, z, rotationY };
	});
}
