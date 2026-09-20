/**
 * @file boardRotation.ts
 * @brief The single rotation the whole table shares during a spectator spin.
 *
 * The board is authored as a collection of world-space coordinates whose
 * canonical orientation is "the viewed player's seat is at the bottom, yaw 0".
 * A spectator spin is therefore not a property of any one piece of furniture —
 * it is one yaw applied to the whole table at once. Everything that belongs to
 * the table (playmat, turn arrows, both piles, every seat) lives under one
 * group carrying this angle; the only exceptions are the local hand row and the
 * card flights, which are handled separately (see AllCards3D / baseBeats).
 *
 * Keeping this a pure function of (x, z, yaw) — no Svelte, no three.js — means
 * the renderer, the animation layer's flight anchors and the tests all share
 * exactly the same transform instead of each re-deriving a rotation matrix.
 */

/** A point or pose on the board plane (Y is carried separately, never rotated). */
export interface BoardPoint {
	x: number;
	z: number;
}

/**
 * Rotates a board-plane point about the mat's center (the world origin) by
 * `yaw` radians. Three.js' standard Y-axis rotation, matching a parent group's
 * `rotation.y`: worldX = x*cos + z*sin, worldZ = -x*sin + z*cos.
 */
export function rotateBoardPoint(point: BoardPoint, yaw: number): BoardPoint {
	if (yaw === 0) return { x: point.x, z: point.z };
	const cos = Math.cos(yaw);
	const sin = Math.sin(yaw);
	return {
		x: point.x * cos + point.z * sin,
		z: -point.x * sin + point.z * cos
	};
}

/**
 * Rotates the [x, y, z] tuple form used by card poses and flight anchors, about
 * the board origin. Y is untouched — a spin turns the table about its vertical
 * axis, it never tilts it.
 */
export function rotateBoardPose(
	pose: readonly [number, number, number],
	yaw: number
): [number, number, number] {
	if (yaw === 0) return [pose[0], pose[1], pose[2]];
	const { x, z } = rotateBoardPoint({ x: pose[0], z: pose[2] }, yaw);
	return [x, pose[1], z];
}

/**
 * Rotates a card's own in-plane spin by the board yaw. A card rendered on the
 * rotating table must turn WITH it, or every card would stay facing the same
 * screen direction while its position orbits — the "cards slide around a static
 * picture" artifact this whole module exists to avoid.
 */
export function rotateBoardSpinDeg(spinDeg: number, yaw: number): number {
	return spinDeg + (yaw * 180) / Math.PI;
}

/**
 * The starting yaw for a spin that brings `from` around to the bottom pivot.
 * `spinAngleBetween`'s replacement after the orbital model was removed: the
 * whole table turns by the incoming seat's bearing so that seat's own world
 * position swings down to where the local seat sits.
 *
 * `seat` is the incoming player's committed seat position; the bottom pivot is
 * the local seat at `(0, localAvatarZ)`. Returns a shortest-path angle in
 * (-π, π].
 */
export function boardRotationFor(seat: BoardPoint, pivot: BoardPoint): number {
	const seatAngle = Math.atan2(seat.z, seat.x);
	const pivotAngle = Math.atan2(pivot.z, pivot.x);
	// Under this module's convention (see rotateBoardPoint) rotating a point by
	// θ SUBTRACTS θ from its atan2(z, x) bearing, so the θ that lands the seat on
	// the pivot's bearing is the seat's bearing minus the pivot's.
	let delta = seatAngle - pivotAngle;
	const TAU = Math.PI * 2;
	delta = ((delta % TAU) + TAU) % TAU;
	if (delta > Math.PI) delta -= TAU;
	return delta;
}
