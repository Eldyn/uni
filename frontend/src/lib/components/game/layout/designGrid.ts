/**
 * @file designGrid.ts
 * @brief The board's canonical composition, expressed as a row/column grid
 * instead of freestanding world-unit constants, so the camera and every
 * seat/pile/card position read as one intentional layout rather than ad-hoc
 * numbers. 1 grid unit === 1 world unit (seatLayout3D.ts's ring radii,
 * three/units.ts's LOCAL_SEAT_Z/CARD_WIDTH etc. are already expressed in
 * this unit) — this module only turns "how many units of coverage does the
 * board need" into "how big a cell can the current screen afford", i.e. the
 * scale factor a top-down orthographic camera uses to match the viewport.
 *
 * boardExtentsFor gives the full board's reference size per orientation: the
 * opponent ring's full width and depth, mirrored about the world origin.
 * Whichever axis the current aspect ratio is more constrained on wins
 * ("contain" fit) — the camera's frustum always matches the viewport's exact
 * aspect ratio (no distortion, no unused letterbox bars), it just shows a few
 * extra empty columns/rows on the non-constrained axis rather than cropping.
 *
 * The grid is always CENTERED on the world origin: the playmat and the discard
 * pile live there, and they are the board's visual anchor, so they belong at
 * the exact center of the screen on every aspect ratio. The local hand isn't
 * part of these extents at all — boardPlacement.ts drops it just inside
 * whatever bottom edge the resulting frustum has, which keeps it flush with
 * the screen edge without dragging the origin off-center to do it.
 */

// ringRadiiFor's count default is MAX_OPPONENTS — the frustum is always sized
// for the widest ring so a mid-game join never reframes the whole board.
import { ringRadiiFor, ringReachFor } from "./seatLayout3D";
import type { ViewportInfo } from "./seatLayout";

// Re-exported for existing consumers — this module used to own the reach
// constants outright; they moved to seatLayout3D.ts so ringRadiiFor could use
// them too without an import cycle (see that file for the definition).
export { ringReachFor };

export interface DesignGrid {
	/** World units visible from the frustum center to its left/right edge. */
	halfWidthUnits: number;
	/** World units visible from the frustum center to its top/bottom edge. */
	halfHeightUnits: number;
	/** World Z the grid is vertically centered on — always the origin. */
	centerZ: number;
}

export interface BoardExtents {
	/** Full board width the frustum must cover, in world units. */
	columns: number;
	/** Full board depth the frustum must cover, in world units. */
	rows: number;
}

/**
 * The board's reference row/column coverage for the given orientation, as a
 * box centered on the world origin. The ring reaches the same distance in
 * every direction from the mat's center, so mirroring its far edge is what
 * keeps that center on the screen's center.
 */
export function boardExtentsFor(viewport: ViewportInfo): BoardExtents {
	const { rx, rz } = ringRadiiFor(viewport);
	const reach = ringReachFor(viewport);
	return {
		columns: (rx + reach) * 2,
		rows: (rz + reach) * 2
	};
}

/**
 * Fits the board's columns × rows into the given viewport's aspect ratio
 * without cropping either axis, then applies `zoom` (already clamped by the
 * caller) as a magnification factor on top.
 */
export function computeDesignGrid(viewport: ViewportInfo, zoom: number): DesignGrid {
	const { columns, rows } = boardExtentsFor(viewport);
	const aspect = viewport.width / viewport.height;
	const halfHeightUnits = Math.max(rows / 2, columns / 2 / aspect) / zoom;

	return {
		halfWidthUnits: halfHeightUnits * aspect,
		halfHeightUnits,
		centerZ: 0
	};
}
