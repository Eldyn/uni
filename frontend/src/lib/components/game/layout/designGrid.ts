/**
 * @file designGrid.ts
 * @brief The board's canonical composition, expressed as a row/column grid
 * instead of freestanding world-unit constants, so the camera and every
 * seat/pile/card position read as one intentional layout rather than ad-hoc
 * numbers. 1 grid unit === 1 world unit (seatLayout3D.ts's SEAT_RING_RADIUS,
 * three/units.ts's LOCAL_SEAT_Z/CARD_WIDTH etc. are already expressed in
 * this unit) — this module only turns "how many units of coverage does the
 * board need" into "how big a cell can the current screen afford", i.e. the
 * scale factor a top-down orthographic camera uses to match the viewport.
 *
 * GRID_ROWS/GRID_COLUMNS are the full board's reference size: enough rows to
 * span the far opponent seat down through the local hand, enough columns to
 * span the ring's full width. Whichever axis the current aspect ratio is
 * more constrained on wins ("contain" fit) — the camera's frustum always
 * matches the viewport's exact aspect ratio (no distortion, no unused
 * letterbox bars), it just shows a few extra empty columns/rows on the
 * non-constrained axis rather than ever cropping content.
 */

import { SEAT_RING_RADIUS } from "./seatLayout3D";
import { LOCAL_SEAT_Z } from "../three/units";
import type { ViewportInfo } from "./seatLayout";

export interface DesignGrid {
	/** World units visible from the frustum center to its left/right edge. */
	halfWidthUnits: number;
	/** World units visible from the frustum center to its top/bottom edge. */
	halfHeightUnits: number;
	/** World Z the grid is vertically centered on. */
	centerZ: number;
}

// Columns: the ring's own diameter plus a card width and the far-side label.
export const GRID_COLUMNS = (SEAT_RING_RADIUS + 1.5) * 2;

// Rows: from the farthest opponent seat down through the local hand's
// forward-most card (handFan.ts's own reach plus a card's half-height).
const FAR_ROW_EDGE = -SEAT_RING_RADIUS;
const NEAR_ROW_EDGE = LOCAL_SEAT_Z + 2.5;
export const GRID_ROWS = NEAR_ROW_EDGE - FAR_ROW_EDGE;
export const GRID_CENTER_Z = (FAR_ROW_EDGE + NEAR_ROW_EDGE) / 2;

/**
 * Fits the GRID_COLUMNS × GRID_ROWS board into the given viewport's aspect
 * ratio without cropping either axis, then applies `zoom` (already clamped
 * by the caller) as a magnification factor on top.
 */
export function computeDesignGrid(viewport: ViewportInfo, zoom: number): DesignGrid {
	const aspect = viewport.width / viewport.height;
	const halfHeightUnits =
		Math.max(GRID_ROWS / 2, GRID_COLUMNS / 2 / aspect) / zoom;

	return {
		halfWidthUnits: halfHeightUnits * aspect,
		halfHeightUnits,
		centerZ: GRID_CENTER_Z
	};
}
