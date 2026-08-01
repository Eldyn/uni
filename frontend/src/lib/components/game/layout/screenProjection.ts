/**
 * @file screenProjection.ts
 * @brief Projects a world-space point on the playmat plane to a screen-space
 * percentage, for DOM elements (GameBoard.svelte's invisible pile anchors)
 * that have to sit where a WebGL mesh visually is without themselves being
 * WebGL. The board's camera is a straight top-down orthographic view with no
 * rotation about Y (see cameraRig.ts), so world X maps linearly to screen X
 * and world Z maps linearly to screen Y, both centered on the camera's own
 * look-at point — there is no perspective divide, no roll, nothing a real
 * three.js `Vector3.project()` would give you here that this doesn't.
 *
 * Every DOM anchor on the board should go through this, rather than each
 * caller re-deriving its own pixels-per-world-unit conversion: a previous
 * version of GameBoard.svelte hand-rolled that conversion once for its draw
 * pile anchor and hardcoded "50%, 50%" for its discard pile anchor (silently
 * assuming the discard sits at world origin), which is exactly the kind of
 * assumption this function makes explicit and computed instead.
 */

import type { CameraRig } from "./cameraRig";

export interface ScreenPercent {
	/** Percent (0-100) from the viewport's left edge. */
	leftPercent: number;
	/** Percent (0-100) from the viewport's top edge. */
	topPercent: number;
}

/**
 * @param worldX World-space X of the point to project.
 * @param worldZ World-space Z of the point to project.
 */
export function worldToScreenPercent(
	rig: CameraRig,
	worldX: number,
	worldZ: number
): ScreenPercent {
	return {
		leftPercent: 50 + ((worldX - rig.lookAt[0]) / (2 * rig.halfWidth)) * 100,
		topPercent: 50 + ((worldZ - rig.lookAt[2]) / (2 * rig.halfHeight)) * 100
	};
}
