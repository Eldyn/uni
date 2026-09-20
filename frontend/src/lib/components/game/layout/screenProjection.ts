/**
 * @file screenProjection.ts
 * @brief Projects a world-space point on the playmat plane to a screen-space
 * percentage, for DOM elements (GameBoard.svelte's action popups, the sort
 * button) that have to sit where a WebGL mesh visually is without themselves
 * being WebGL.
 *
 * The board is viewed through a tilted PERSPECTIVE camera (see cameraRig.ts),
 * so there is no longer a linear world→screen mapping: a point farther from the
 * camera is smaller and closer to the vanishing point. This therefore goes
 * through three.js' own `Vector3.project()` rather than re-deriving pixels-per-
 * world-unit, which would only be correct for the old orthographic view.
 *
 * A camera is required, so this is only usable once the scene has one; callers
 * that need it before mount should fall back to a sensible default (the DOM
 * anchor is decoration, not layout-critical).
 */

import { Vector3, type PerspectiveCamera } from "three";

export interface ScreenPercent {
	/** Percent (0-100) from the viewport's left edge. */
	leftPercent: number;
	/** Percent (0-100) from the viewport's top edge. */
	topPercent: number;
}

/**
 * @param camera The scene's live perspective camera.
 * @param worldX World-space X of the point to project.
 * @param worldY World-space Y of the point (the playmat plane is y≈0).
 * @param worldZ World-space Z of the point to project.
 */
export function worldToScreenPercent(
	camera: PerspectiveCamera,
	worldX: number,
	worldY: number,
	worldZ: number
): ScreenPercent {
	const projected = new Vector3(worldX, worldY, worldZ).project(camera);
	return {
		leftPercent: (projected.x * 0.5 + 0.5) * 100,
		topPercent: (-projected.y * 0.5 + 0.5) * 100
	};
}
