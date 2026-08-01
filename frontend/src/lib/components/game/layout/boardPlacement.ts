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
import { matBounds } from "./playmat";
import type { ViewportInfo } from "./seatLayout";

export interface BoardPlacement {
	/** Uniform scale for the local hand row and the draw pile beside it. */
	handScale: number;
	/** Scale for the discard pile at the mat's center — the same size as the
	 *  hand's own cards, so the card you're about to play and the card it lands
	 *  on read as the same object. Shrinks below that only when the nearest
	 *  opponent seat is close enough that a full-size pile would collide. */
	centerScale: number;
	/** World Z of the local hand row (and of the draw pile's top card). */
	localSeatZ: number;
	/** World Z of the local player's own avatar, just above the hand row. */
	localAvatarZ: number;
	/** World X of the draw pile's center, left of the hand row. */
	drawPileX: number;
}

// The local avatar's size in WORLD units — the same currency the opponent
// avatars are declared in (see Scene3D), so "every seat's icon is the same
// size" is true by construction instead of by two constants agreeing.
export const LOCAL_AVATAR_WORLD = 0.78;

// base_player.gif is a 96x96 canvas, but the figure only fills part of it, and
// by how much drifts as the idle animation plays — coalescing every frame and
// measuring each one's own opaque bbox (`magick base_player.gif -coalesce ...
// -alpha extract -threshold 10% -format %@`) gives top offsets from 14 to 18px
// and heights from 66 to 68px. The 96x96 CSS mask-size the avatar CSS uses
// zooms in to crop out the padding a fixed 96/70 (see AVATAR_MASK_ZOOM_PERCENT
// there), keeping rows [14, 84] — the union every frame's figure sits inside —
// which is also where this fraction comes from: 68 (the tallest frame) over 70
// (that crop window), not over the full 96. Every avatar size on the board
// describes the FIGURE at that crop, never the frame: sizing by the frame
// leaves the built-in padding showing as dead space around the icon, which
// is both why the icon used to float short of the felt (LOCAL_AVATAR_WORLD)
// and why the padding read as visible empty space around every opponent's.
export const AVATAR_SPRITE_FILL = 68 / 70;

/** CSS px for an avatar box drawing a figure `worldSize` tall. */
export function avatarBoxPx(worldSize: number, worldPerPx: number): number {
	return Math.round(worldSize / AVATAR_SPRITE_FILL / worldPerPx);
}

// Gap between the local avatar's near edge and the hand row's far edge.
export const LOCAL_SEAT_GAP = 0.55;

// Gap left between the hand row's near edge and the screen's bottom edge.
export const HAND_BOTTOM_MARGIN = 0.3;

// The hand's cards are sized so the player's own avatar comes to rest exactly
// under the felt's near edge, touching it with its own far (mat-facing) edge
// rather than standing on top of it: the hand row, the whole avatar and the
// bottom margin together fill the strip between the felt and the screen's
// bottom, so there is no dead band under the table and the player's cards are
// as big as that strip allows. An earlier version matched the avatar's NEAR
// edge to the felt instead (only half the avatar counted against the strip),
// which left the avatar's far half standing on the felt itself and made the
// hand's cards read as oversized — matching the far edge here keeps the whole
// figure below the felt line. Bounded so a very tall or very square window
// can't solve for a card that eats the board — at that point the strip is
// bigger than a hand needs.
export const MIN_HAND_SCALE = 1;
export const MAX_HAND_SCALE = 3;

export const MIN_CENTER_SCALE = 0.85;
// Pile cards are jittered and rotated, so their real footprint runs past a
// single upright card's half-height; this covers the overhang.
export const CENTER_RING_MARGIN = 0.35;

// The draw pile's resting place beside the hand on a wide screen. On a narrow
// one that X would fall outside the frustum, so it slides inward to sit one
// card-half plus a margin inside the left edge instead.
export const DRAW_PILE_HOME_X = -5.5;
export const DRAW_PILE_EDGE_MARGIN = 0.2;

/**
 * @param centerClearanceZ World-space depth available between the mat's center
 * and the nearest opponent seat's outermost card (see handRing.ts's
 * opponentSeatReachWorld). Omit it to leave the center pile at its full size.
 */
export function computeBoardPlacement(
	_viewport: ViewportInfo,
	rig: CameraRig,
	centerClearanceZ: number = Infinity
): BoardPlacement {
	// Solve the strip below the felt for the card height that fills it:
	//   avatarFarEdge == feltNearEdge, where the avatar sits one SEAT_GAP plus
	//   one full card height above the bottom margin, and its own far edge is
	//   a further half-avatar above its own center.
	const strip =
		rig.halfHeight -
		HAND_BOTTOM_MARGIN -
		LOCAL_SEAT_GAP -
		LOCAL_AVATAR_WORLD / 2 -
		matBounds(rig.halfWidth, rig.halfHeight).near;
	const handScale = Math.min(MAX_HAND_SCALE, Math.max(MIN_HAND_SCALE, strip / CARD_HEIGHT));

	const centerScale = Math.min(
		handScale,
		Math.max(MIN_CENTER_SCALE, ((centerClearanceZ - CENTER_RING_MARGIN) * 2) / CARD_HEIGHT)
	);

	const localSeatZ = rig.halfHeight - (CARD_HEIGHT * handScale) / 2 - HAND_BOTTOM_MARGIN;
	const localAvatarZ = localSeatZ - (CARD_HEIGHT * handScale) / 2 - LOCAL_SEAT_GAP;
	const drawPileX = Math.max(
		DRAW_PILE_HOME_X,
		-(rig.halfWidth - (CARD_WIDTH * handScale) / 2 - DRAW_PILE_EDGE_MARGIN)
	);

	return { handScale, centerScale, localSeatZ, localAvatarZ, drawPileX };
}
