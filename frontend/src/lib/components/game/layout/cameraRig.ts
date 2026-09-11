/**
 * @file cameraRig.ts
 * @brief Pure camera-placement solver for the Threlte board. No Threlte, no
 * three.js — just the numbers a <T.OrthographicCamera> needs, so the framing
 * math stays unit-testable like seatLayout.ts/seatLayout3D.ts.
 *
 * The board is a straight top-down view — no perspective on any seat's
 * cards or the local hand — so the camera is orthographic, positioned
 * directly above the playmat and looking straight down (-Y). Its frustum
 * size comes from designGrid.ts's canonical row/column board, fit to the
 * current viewport's exact aspect ratio (see that file for the "contain"
 * fit rationale).
 */

import { computeDesignGrid } from "./designGrid";
import type { ViewportInfo } from "./seatLayout";

export interface CameraRig {
	/** World-space camera position (straight above the look-at point). */
	position: [number, number, number];
	/** World-space point the camera looks straight down at. */
	lookAt: [number, number, number];
	/** Half-width of the orthographic frustum, in world units. */
	halfWidth: number;
	/** Half-height of the orthographic frustum, in world units. */
	halfHeight: number;
	/** World Z the frustum is centered on. Zero in landscape; pushed toward the
	 *  near side in portrait, where the width-bound fit leaves vertical slack
	 *  that is worth more under the table than above it (see designGrid.ts).
	 *  Anything measuring from a frustum EDGE must offset by this. */
	centerZ: number;
}

// How high above the table the camera sits. An orthographic camera's
// apparent zoom doesn't depend on this (only halfWidth/halfHeight do) — it
// only needs to clear every card/avatar mesh so nothing pokes through.
const CAMERA_HEIGHT = 20;

const MIN_ZOOM = 0.5;
const MAX_ZOOM = 2;

/**
 * Computes the top-down orthographic camera rig for a given viewport.
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

	return {
		position: [0, CAMERA_HEIGHT, grid.centerZ],
		lookAt: [0, 0, grid.centerZ],
		halfWidth: grid.halfWidthUnits,
		halfHeight: grid.halfHeightUnits,
		centerZ: grid.centerZ
	};
}
