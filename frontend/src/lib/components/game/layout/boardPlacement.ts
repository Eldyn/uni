/**
 * @file boardPlacement.ts
 * @brief Where the local player's own furniture — the hand row, the draw pile —
 * sits inside whatever frustum cameraRig.ts ended up with, plus how big its
 * cards are. Split out from designGrid.ts because the dependency runs the other
 * way: the grid is sized from the opponent ring alone and stays centered on the
 * playmat (see designGrid.ts), and only then does the local hand get dropped
 * just inside the resulting bottom edge. Sizing the frustum around the hand
 * instead is what used to drag the mat off the screen's center.
 *
 * Everything here is a continuous function of the frustum, so a phone and an
 * ultrawide monitor get the same composition rather than two tuned cases.
 */

import { CARD_HEIGHT, CARD_WIDTH } from "../three/units";
import type { CameraRig } from "./cameraRig";
import type { ViewportInfo } from "./seatLayout";

export interface BoardPlacement {
	/** Uniform scale for the local hand, the draw pile and the discard pile. */
	handScale: number;
	/** World Z of the local hand row (and of the draw pile's top card). */
	localSeatZ: number;
	/** World X of the draw pile's center, left of the hand row. */
	drawPileX: number;
}

// The local hand's card size on a normal landscape screen. Its own cards are
// the ones the player actually reads, so they run well above the world's
// 1-unit card baseline that the opponent rings use.
export const BASE_HAND_SCALE = 1.35;

// A phone shows the same hand across a fraction of the width, so its cards have
// to claim a larger share of the screen to stay readable at arm's length. The
// boost grows as the viewport narrows past square, but it competes with itself
// past this cap: a bigger card also widens the gap boardPlacement reserves to
// the draw pile, eating into the very half-span the hand row needs to spread
// cards apart at all — past ~1.2x that feedback collapses a phone's hand into
// one visible card with everything else faded to a sliver behind it.
export const HAND_BOOST_REFERENCE_ASPECT = 1;
export const MAX_HAND_BOOST = 1.2;

// Gap left between the hand row's near edge and the screen's bottom edge.
export const HAND_BOTTOM_MARGIN = 0.3;

// The draw pile's resting place beside the hand on a wide screen. On a narrow
// one that X would fall outside the frustum, so it slides inward to sit one
// card-half plus a margin inside the left edge instead.
export const DRAW_PILE_HOME_X = -5.5;
export const DRAW_PILE_EDGE_MARGIN = 0.2;

export function computeBoardPlacement(viewport: ViewportInfo, rig: CameraRig): BoardPlacement {
	const aspect = viewport.width / viewport.height;
	const boost = Math.min(MAX_HAND_BOOST, Math.max(1, HAND_BOOST_REFERENCE_ASPECT / aspect));
	const handScale = BASE_HAND_SCALE * boost;

	const localSeatZ = rig.halfHeight - (CARD_HEIGHT * handScale) / 2 - HAND_BOTTOM_MARGIN;
	const drawPileX = Math.max(
		DRAW_PILE_HOME_X,
		-(rig.halfWidth - (CARD_WIDTH * handScale) / 2 - DRAW_PILE_EDGE_MARGIN)
	);

	return { handScale, localSeatZ, drawPileX };
}
