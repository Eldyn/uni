/**
 * @file cameraRig.ts
 * @brief Pure camera-placement solver for the Threlte board. No Threlte, no
 * three.js — just the numbers a <T.PerspectiveCamera> needs, so the framing
 * math stays unit-testable like seatLayout.ts/seatLayout3D.ts.
 *
 * The camera looks down at the table from a small tilt off vertical (see
 * CAMERA_TILT_DEG). That tilt is what gives the board its depth: a stack of
 * cards has a real vertical extent, and only a camera that is not perfectly
 * overhead can see its sides — the "tower with visible edges" the spectator
 * spin needs. A straight-down orthographic view showed the same silhouette at
 * every rotation angle, which is why the spin never read as a rotation.
 *
 * The board's world-space COVERAGE still comes from designGrid.ts's canonical
 * row/column layout, fit to the viewport's exact aspect ratio. `halfWidth` /
 * `halfHeight` below are that coverage measured at the board plane, so every
 * layout module (boardPlacement, handSlotPose, handLine, seatLayout3D) keeps
 * working in the same world units it always did; only the projection that turns
 * those units into pixels changed.
 */

import { computeDesignGrid } from "./designGrid";
import type { ViewportInfo } from "./seatLayout";

export interface CameraRig {
	/** World-space camera position (above and slightly toward the local side). */
	position: [number, number, number];
	/** World-space point the camera looks at. */
	lookAt: [number, number, number];
	/** Half-width of the board's world coverage at the board plane. */
	halfWidth: number;
	/** Half-height of the board's world coverage at the board plane. */
	halfHeight: number;
	/** World Z the framed board is centered on. Zero in landscape; pushed
	 *  toward the near side in portrait, where the width-bound fit leaves
	 *  vertical slack that is worth more under the table than above it (see
	 *  designGrid.ts). Anything measuring from a frustum EDGE offsets by this. */
	centerZ: number;
	/** Vertical field of view of the perspective camera, degrees. */
	fov: number;
	/** Distance from `lookAt` to `position`, world units. */
	distance: number;
	/** Tilt off straight-down, radians (0 = the old top-down view). */
	tiltRad: number;
	/** Near/far clip planes for the perspective camera. */
	near: number;
	far: number;
}

// A small tilt: enough that a card stack has a visible side and the mat reads
// as a real plane, while the board still composes almost like the old top-down
// view. Kept low on purpose — a stronger tilt compresses the far seats and
// turns the board into a diorama. Tuned against screenshots.
export const CAMERA_TILT_DEG = 6;

// A narrower field of view reads as "further away and flatter": at the same
// near-edge framing the camera sits further back, so the perspective divide
// varies less across the board and the near/far size disparity that made the
// old 50° view feel extreme is dialled right back. Depth still reads on tall
// stacks (their near edge is genuinely closer); the table itself flattens.
export const CAMERA_FOV_DEG = 38;

// Near/far derive from the camera's distance; these are the allowances around
// it. The board's tallest stack is ~1.2 world units and avatars sit at y≈0, so
// a near plane a few units in front of the board and a far plane well past the
// camera cover everything without z-fighting the depth buffer.
const NEAR_MARGIN = 4;
const FAR_MARGIN = 60;

// Guard rails for the solved distance, so an extreme aspect can't put the
// camera inside the table or at orbit height.
const MIN_DISTANCE = 8;
const MAX_DISTANCE = 120;

const MIN_ZOOM = 0.5;
const MAX_ZOOM = 2;

/**
 * Computes the tilted perspective camera rig for a given viewport.
 * `zoom` is a clamped magnification multiplier (>1 = pinch-in closer, <1 =
 * pinch-out farther) for future touch-zoom support; omit it for the default
 * framing. `opponentCount` is accepted for API symmetry with the rest of the
 * layout modules and future per-count framing tweaks, but designGrid.ts sizes
 * the board extents for the max table (ringRadiiFor's count default) so a
 * mid-game join never reframes the whole board — count isn't required for
 * correctness today.
 */
export function computeCameraRig(
	viewport: ViewportInfo,
	_opponentCount: number,
	zoom: number = 1
): CameraRig {
	const clampedZoom = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, zoom));
	const grid = computeDesignGrid(viewport, clampedZoom);

	const fov = CAMERA_FOV_DEG;
	const tiltRad = (CAMERA_TILT_DEG * Math.PI) / 180;

	// The coverage above is measured at the LOOK-AT plane. A tilted perspective
	// camera does not preserve it: the near edge of the board is closer to the
	// camera and projects LARGER, the far edge SMALLER. Sizing the distance from
	// the center-plane half-height alone therefore pushed the board's near edge
	// (and the local hand row sitting on it) clean past the bottom of the screen.

	// Solve instead for the distance at which the board's NEAR edge exactly
	// touches the bottom of the frustum — the binding constraint under a
	// downward tilt. With the camera at `(0, cosT·d, sinT·d)` relative to the
	// look-at point looking along `(0, -cosT, -sinT)`, a table point at depth z
	// has axis distance `d - sinT·z` and vertical offset `-cosT·z`; requiring the
	// near edge (`z = halfHeight`) to reach the frustum's bottom gives
	//   halfHeight = (cosT · halfHeight) / ((d - sinT · halfHeight) · tan(fov/2))
	// and solving for d yields the closed form below.
	const halfFovRad = (fov * Math.PI) / 360;
	const halfHeight = grid.halfHeightUnits;
	const cosT = Math.cos(tiltRad);
	const sinT = Math.sin(tiltRad);
	const tanHalfFov = Math.tan(halfFovRad);

	const rawDistance = halfHeight * (cosT / tanHalfFov + sinT);
	const distance = Math.min(MAX_DISTANCE, Math.max(MIN_DISTANCE, rawDistance));

	// Look at the board center, sit above and toward the local (+Z) side by the
	// tilt, so the far edge of the mat is seen at a shallow angle.
	const lookAt: [number, number, number] = [0, 0, grid.centerZ];
	const position: [number, number, number] = [
		0,
		lookAt[1] + Math.cos(tiltRad) * distance,
		lookAt[2] + Math.sin(tiltRad) * distance
	];

	return {
		position,
		lookAt,
		halfWidth: grid.halfWidthUnits,
		halfHeight: grid.halfHeightUnits,
		centerZ: grid.centerZ,
		fov,
		distance,
		tiltRad,
		near: Math.max(0.1, distance - NEAR_MARGIN),
		far: distance + FAR_MARGIN
	};
}

/** A transient world-space nudge on top of the rig's base camera position. */
export interface CameraOffset {
	x: number;
	y: number;
	z: number;
}

export const ZERO_CAMERA_OFFSET: Readonly<CameraOffset> = Object.freeze({ x: 0, y: 0, z: 0 });

/** The rig's camera position moved by `offset`. Orientation is untouched, so
 *  an offset along the view axis dollies without re-aiming the camera. */
export function offsetCameraPosition(
	rig: CameraRig,
	offset: Readonly<CameraOffset>
): [number, number, number] {
	return [rig.position[0] + offset.x, rig.position[1] + offset.y, rig.position[2] + offset.z];
}

/** An offset of `distance` world units along the view axis, toward the table.
 *  The axis only depends on the fixed tilt, never on the viewport. */
export function cameraPunchOffset(distance: number): CameraOffset {
	const tiltRad = (CAMERA_TILT_DEG * Math.PI) / 180;
	return {
		x: 0,
		y: -Math.cos(tiltRad) * distance,
		z: -Math.sin(tiltRad) * distance
	};
}
