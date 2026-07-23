/**
 * @file cameraRig.ts
 * @brief Pure camera-placement solver for the Threlte board. No Threlte, no
 * three.js — just the numbers a <T.PerspectiveCamera> needs, so the framing
 * math stays unit-testable like seatLayout.ts/seatLayout3D.ts.
 *
 * seatLayout3D.ts's opponent ring has a FIXED world radius regardless of
 * player count (only the angles change), so unlike the DOM board's
 * count-based scale-down, correctness here doesn't depend on opponentCount
 * at all — it depends on the viewport's aspect ratio. A `PerspectiveCamera`'s
 * `fov` is vertical; on a narrow portrait phone the horizontal frustum
 * (vfov scaled by the aspect ratio) is much tighter, so a distance/fov pair
 * tuned for landscape clips the left/right seats off-screen in portrait.
 * `reach` (the camera's straight-line distance to the playmat center) is
 * solved from the aspect ratio so the ring's world radius always fits
 * horizontally, then split into height/distance to set the pitch angle.
 */

import { SEAT_RING_RADIUS } from "./seatLayout3D";
import type { ViewportInfo } from "./seatLayout";

export interface CameraRig {
	/** World-space camera position. */
	position: [number, number, number];
	/** Vertical field of view, in degrees. */
	fov: number;
	/** World-space point the camera is aimed at. */
	lookAt: [number, number, number];
}

// Half-width the camera must be able to see at the ring's distance: the
// ring's own radius plus room for a card's width and the far-side name label.
const RING_COVERAGE = SEAT_RING_RADIUS + 1.5;

const LANDSCAPE_FOV = 45;
const PORTRAIT_FOV = 65; // wider — a narrow aspect needs more vfov to keep the same horizontal coverage

// Fraction of `reach` (camera-to-target distance) that becomes camera height,
// i.e. sin(pitch angle). Portrait pitches steeper (more top-down) so the
// ring reframes toward a vertical oval instead of running off the sides.
const LANDSCAPE_PITCH_RATIO = 0.6;
const PORTRAIT_PITCH_RATIO = 0.75;

// However close a wide/short viewport's math would put the camera, never
// let it dolly inside the ring itself.
const MIN_REACH = SEAT_RING_RADIUS + 2;

const MIN_ZOOM = 0.5;
const MAX_ZOOM = 2;

// Portrait pulls the look-at target toward the far seats so the opponent
// ring reads near the top of the frame, leaving the bottom of the viewport
// clear for the local hand (a DOM overlay, not part of this scene).
const PORTRAIT_LOOKAT_Z = -1.5;
const LANDSCAPE_LOOKAT_Z = 0;

/**
 * Computes where the board camera should sit for a given viewport and
 * opponent count. `zoom` is a clamped dolly multiplier (>1 = pinch-in
 * closer, <1 = pinch-out farther) for future touch-zoom support; omit it
 * for the default framing.
 */
export function computeCameraRig(
	viewport: ViewportInfo,
	_opponentCount: number,
	zoom: number = 1
): CameraRig {
	const isPortrait = viewport.orientation === "portrait";
	const aspect = viewport.width / viewport.height;
	const clampedZoom = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, zoom));

	const fov = isPortrait ? PORTRAIT_FOV : LANDSCAPE_FOV;
	const halfVfovRad = (fov * Math.PI) / 360;

	// Straight-line camera-to-target distance that puts RING_COVERAGE exactly
	// at the edge of the horizontal frustum (vfov scaled by aspect = hfov).
	const reach = Math.max(MIN_REACH, RING_COVERAGE / (Math.tan(halfVfovRad) * aspect)) / clampedZoom;

	const pitchRatio = isPortrait ? PORTRAIT_PITCH_RATIO : LANDSCAPE_PITCH_RATIO;
	const height = reach * pitchRatio;
	const distance = Math.sqrt(Math.max(reach * reach - height * height, 0));
	const lookAtZ = isPortrait ? PORTRAIT_LOOKAT_Z : LANDSCAPE_LOOKAT_Z;

	return {
		position: [0, height, distance],
		fov,
		lookAt: [0, 0, lookAtZ]
	};
}
