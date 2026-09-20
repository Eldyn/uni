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
import { CROSS_OPPONENT_COUNT, MAX_OPPONENTS, type ViewportInfo } from "./seatLayout";

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
function extentsAtCount(viewport: ViewportInfo, opponentCount: number): BoardExtents {
	const { rx, rz } = ringRadiiFor(viewport, opponentCount);
	const reach = ringReachFor(viewport, opponentCount);
	return {
		columns: (rx + reach) * 2,
		rows: (rz + reach) * 2
	};
}

export function boardExtentsFor(viewport: ViewportInfo): BoardExtents {
	// The frustum has to hold every seat count, not just the fullest table: rx
	// GROWS toward a full arch while reach SHRINKS with it, so neither endpoint
	// dominates both axes. Sizing off the crowded end alone is what clipped a
	// 4-player game's top seat — a sparse table's reach is twice a full one's,
	// and that difference is exactly the margin its labels and ring needed.
	// Both terms are linear in the same crowding ratio, so the max over the
	// whole range is always at one of the two endpoints.
	const sparse = extentsAtCount(viewport, CROSS_OPPONENT_COUNT);
	const crowded = extentsAtCount(viewport, MAX_OPPONENTS);
	return {
		columns: Math.max(sparse.columns, crowded.columns),
		rows: Math.max(sparse.rows, crowded.rows)
	};
}

/**
 * Fits the board's columns × rows into the given viewport's aspect ratio
 * without cropping either axis, then applies `zoom` (already clamped by the
 * caller) as a magnification factor on top.
 */
// A portrait frustum is width-bound by a long way — fitting the ring's columns
// into a 0.46 aspect leaves the vertical axis with far more world than the
// board's rows need. Split evenly (centerZ 0) that slack lands half above the
// table, where nothing is drawn, and half below, where the hand already sits
// flush against the bottom edge; the top half reads as a dead band under the
// HUD. Pushing the camera toward the near side moves most of it below the
// table, where the hand row can actually spend it. Not the whole slack: the
// arch of opponent seats lives above the mat and still needs headroom.
const PORTRAIT_NEAR_BIAS = 0.55;

export function computeDesignGrid(viewport: ViewportInfo, zoom: number): DesignGrid {
	const { columns, rows } = boardExtentsFor(viewport);
	const aspect = viewport.width / viewport.height;
	const halfHeightUnits = Math.max(rows / 2, columns / 2 / aspect) / zoom;

	const verticalSlack = Math.max(0, halfHeightUnits - rows / 2);
	const centerZ = viewport.orientation === "portrait" ? verticalSlack * PORTRAIT_NEAR_BIAS : 0;

	return {
		halfWidthUnits: halfHeightUnits * aspect,
		halfHeightUnits,
		centerZ
	};
}
