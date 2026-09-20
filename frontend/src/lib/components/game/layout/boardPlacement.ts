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

import { CARD_HEIGHT, CARD_WIDTH, EM_TO_WORLD } from "../three/units";
import { MAX_JITTER_EM } from "./discardPile";
import type { CameraRig } from "./cameraRig";
import {
	landscapeMatPlacement,
	matBounds,
	portraitMatPlacement,
	PORTRAIT_MAT_WIDTH_FILL,
	type MatPlacement
} from "./playmat";
import type { ViewportInfo } from "./seatLayout";

export interface BoardPlacement {
	/** Where the playmat sheet is drawn, and where its felt lands. */
	mat: MatPlacement;
	/** Uniform scale for the local hand row (and, on portrait, the draw pile
	 *  beside it). */
	handScale: number;
	/** Scale for the discard pile — the same size as the hand's own cards, so
	 *  the card you're about to play and the card it lands on read as the same
	 *  object. Shrinks below that only when the nearest opponent seat is close
	 *  enough that a full-size pile would collide. */
	centerScale: number;
	/** World X of the discard pile's center. Zero on portrait (the felt's center
	 *  line); on landscape it is the right-hand half of the center pile PAIR,
	 *  offset from the mat's center line by the pile spread. */
	discardX: number;
	/** World Z of the discard pile's center. Zero on landscape (the screen's
	 *  center line, where both center piles sit); on portrait it's the felt's
	 *  true midpoint, since there the felt is drawn to an explicit box rather
	 *  than filling the whole frustum. */
	discardZ: number;
	/** World Z of the local hand row. */
	localSeatZ: number;
	/** World Z of the local player's own avatar, just above the hand row. */
	localAvatarZ: number;
	/** World X of the draw pile's center. On landscape it is the left-hand half
	 *  of the center pile pair (left of the mat's center line); on portrait it
	 *  slides in from the left edge to sit beside the hand. */
	drawPileX: number;
	/** World Z of the draw pile's center. Zero on landscape (it shares the
	 *  discard pile's center line); on portrait it equals the hand row's own Z,
	 *  where the pile sits beside the hand. */
	drawPileZ: number;
	/** Scale for the draw pile's cards. Matches the discard pile at the center
	 *  on a wide screen; a fraction of the hand's size on a narrow one, where
	 *  the pile is a small tap target sharing the hand row. */
	drawPileScale: number;
	/** Whether the draw pile shares the hand row (portrait) rather than sitting
	 *  at the mat's center beside the discard pile (landscape). The hand's own
	 *  span solver reads this to know whether the pile eats into its width. */
	drawPileBesideHand: boolean;
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

// The draw pile's resting place beside the hand on portrait. That X would fall
// outside a phone's frustum, so it slides inward to sit one card-half plus a
// margin inside the left edge instead — this is the outward cap it slides in
// from, never a position it actually reaches on a portrait screen.
export const DRAW_PILE_HOME_X = -5.5;
export const DRAW_PILE_EDGE_MARGIN = 0.2;

// Gap left between the two center piles (draw + discard) on a landscape screen.
// Half a card plus the discard scatter's overhang already separates them; this
// is the breathing room on top. Sized generously: the discard stack is jittered
// and rotated, so its real footprint fans out well past a single upright card,
// and at 0.3 the two piles read as one crowded mass rather than two.
export const CENTER_PILE_GAP = 0.9;

// Clearance kept between the DISCARD pile's own scattered footprint and the
// felt's right edge on a landscape screen. The discard pile is clamped by this
// rather than mirrored to the draw pile's X, so the pair is deliberately off
// centre as a group — the pile reads as sitting comfortably on the felt, which
// matters more than the two piles being equidistant from the centre line.
// Measured from the felt's OUTER painted edge, but the art's inner border line
// is inset ~0.9 world units further in, so this has to exceed roughly
// 0.9 + the scatter's reach before the clamp does anything at all; at 0.9 the
// clamp never bound on a 16:9 screen and the scatter sat almost on the inner
// line. 1.9 pulls it a comfortable ~0.9 units clear of that line.
export const CENTER_DISCARD_MARGIN = 1.9;

// Hard floor on the two center piles' separation, in world units. The discard
// clamp above may pull the pair inward on a narrow felt; this stops it pulling
// them into each other.
export const CENTER_PILE_MIN_SPREAD = 1.5;

// Small tap target: on a narrow screen the pile shares the hand row but at a
// fraction of its size, since there it's a target to tap rather than a stack
// to read.
export const PORTRAIT_DRAW_PILE_SCALE = 0.6;

// How much of the frustum's width the hand row may span, and how many cards
// have to fit inside it before the row starts scrolling. Without this the card
// size is solved purely from the height of the strip under the felt, which on a
// tall phone screen is generous enough to produce cards a quarter of the screen
// wide — a readable card that you can only see three of is not a readable hand.
export const HAND_WIDTH_FILL = 0.96;
export const HAND_MIN_VISIBLE_CARDS = 5;

// Portrait draws the felt to an explicit box (see playmat.ts's
// portraitMatPlacement) rather than fitting the sheet, so it needs to know how
// far to hold off the two things bracketing it: the opponent arch's labels at
// the top of the screen, and the local player's own avatar at the bottom.
export const PORTRAIT_MAT_TOP_MARGIN = 1.3;
export const PORTRAIT_MAT_LOCAL_GAP = 0.25;

// Cards overlap at CARD_LINE_SPACING_EM against their own ~5em width; the row's
// total width is therefore one full card plus one spacing per further card.
export const HAND_SPACING_RATIO = 0.8;

/**
 * @param centerClearanceZ World-space depth available between the mat's center
 * and the nearest opponent seat's outermost card (see handRing.ts's
 * opponentSeatReachWorld). Omit it to leave the center pile at its full size.
 */
export function computeBoardPlacement(
	viewport: ViewportInfo,
	rig: CameraRig,
	centerClearanceZ: number = Infinity
): BoardPlacement {
	// Solve the strip below the felt for the card height that fills it:
	//   avatarFarEdge == feltNearEdge, where the avatar sits one SEAT_GAP plus
	//   one full card height above the bottom margin, and its own far edge is
	//   a further half-avatar above its own center.
	// The near (bottom) edge of the frustum in world terms. Portrait pushes the
	// camera toward the player, so the edge is no longer at +halfHeight.
	const nearEdgeZ = rig.centerZ + rig.halfHeight;
	const isPortrait = viewport.orientation === "portrait";
	const strip =
		nearEdgeZ -
		HAND_BOTTOM_MARGIN -
		LOCAL_SEAT_GAP -
		LOCAL_AVATAR_WORLD / 2 -
		matBounds(rig.halfWidth, rig.halfHeight).near;

	// The strip only says how TALL a card may be. A row that fills it can still
	// be too wide to show a hand, so the width the row may span caps the card as
	// well — MIN_HAND_SCALE keeps it readable if a very narrow screen solves for
	// less. Portrait skips the strip entirely: there the dependency runs the
	// other way round, because the felt is drawn to whatever depth the hand
	// leaves rather than the hand being fitted under a fixed felt.
	const rowWidth = 2 * rig.halfWidth * HAND_WIDTH_FILL;
	const widthCap =
		rowWidth / (CARD_WIDTH * (1 + (HAND_MIN_VISIBLE_CARDS - 1) * HAND_SPACING_RATIO));
	const handScale = Math.min(
		MAX_HAND_SCALE,
		Math.max(MIN_HAND_SCALE, isPortrait ? widthCap : Math.min(strip / CARD_HEIGHT, widthCap))
	);

	const centerScale = Math.min(
		handScale,
		Math.max(MIN_CENTER_SCALE, ((centerClearanceZ - CENTER_RING_MARGIN) * 2) / CARD_HEIGHT)
	);

	const localSeatZ = nearEdgeZ - (CARD_HEIGHT * handScale) / 2 - HAND_BOTTOM_MARGIN;
	const localAvatarZ = localSeatZ - (CARD_HEIGHT * handScale) / 2 - LOCAL_SEAT_GAP;
	// Landscape puts BOTH communal piles side by side on the felt's center line
	// — draw on the left, discard on the right — so the middle of the mat carries
	// the whole play area and a spectator spin can pivot around it without a
	// pile to look after. The draw pile therefore no longer shares the hand row
	// (which is what frees the hand to span the full width). Portrait keeps the
	// compact beside-the-hand layout, where there is no room at center.
	const drawPileBesideHand = isPortrait;
	const drawPileScale = isPortrait ? handScale * PORTRAIT_DRAW_PILE_SCALE : handScale;
	// The felt: covering the frustum in landscape (zoomed up a touch, see
	// playmat.ts), and in portrait filling the band between the opponents' arch
	// and the local player's own avatar, so there is no background showing
	// between the table and either seat.
	const mat = isPortrait
		? portraitMatPlacement(
				2 * rig.halfWidth * PORTRAIT_MAT_WIDTH_FILL,
				rig.centerZ - rig.halfHeight + PORTRAIT_MAT_TOP_MARGIN,
				localAvatarZ - LOCAL_AVATAR_WORLD / 2 - PORTRAIT_MAT_LOCAL_GAP
			)
		: landscapeMatPlacement(rig.halfWidth, rig.halfHeight);

	// How far each center pile's center sits from the mat's center line. Each
	// side has to clear half a card plus the discard scatter's own overhang, so
	// it scales with the card size and holds on any viewport.
	const pileCardScale = Math.max(centerScale, drawPileScale);
	const pileSpread =
		(CARD_WIDTH * pileCardScale) / 2 +
		MAX_JITTER_EM * EM_TO_WORLD * pileCardScale +
		CENTER_PILE_GAP;

	const drawPileX = isPortrait
		? Math.max(
				DRAW_PILE_HOME_X,
				-(rig.halfWidth - (CARD_WIDTH * drawPileScale) / 2 - DRAW_PILE_EDGE_MARGIN)
			)
		: -pileSpread;
	// The discard pile is clamped against the MAT's right edge, not mirrored to
	// the draw pile's X. The two piles are deliberately not centred as a pair:
	// the discard's cards are randomly jittered and rotated, so their real
	// footprint fans well right of the pile centre and, at a symmetric spread,
	// visually crowded the felt's right border even though the pile CENTRE was
	// inside it. Pulling it in to keep a clearance from the border reads as more
	// balanced than exact symmetry does. `CENTER_DISCARD_MARGIN` is that
	// clearance, measured from the felt edge to the scatter's own reach.
	const discardReach =
		(CARD_WIDTH * pileCardScale) / 2 + MAX_JITTER_EM * EM_TO_WORLD * pileCardScale;
	const discardMaxX = mat.bounds.right - CENTER_DISCARD_MARGIN - discardReach;
	// A floor on the spread keeps the two piles from ever crowding each other if
	// a very narrow felt clamps the discard hard inward; the clamp still wins
	// over symmetry, it just can't pull the pair into overlap.
	const discardX = isPortrait
		? 0
		: Math.max(CENTER_PILE_MIN_SPREAD, Math.min(pileSpread, discardMaxX));
	const discardZ = isPortrait ? (mat.bounds.far + mat.bounds.near) / 2 : 0;
	const drawPileZ = isPortrait ? localSeatZ : 0;

	return {
		mat,
		discardX,
		discardZ,
		handScale,
		centerScale,
		localSeatZ,
		localAvatarZ,
		drawPileX,
		drawPileZ,
		drawPileScale,
		drawPileBesideHand
	};
}
