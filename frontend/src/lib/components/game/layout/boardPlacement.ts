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
import type { CameraRig } from "./cameraRig";
import { MAX_JITTER_EM, MAX_ROTATION_DEG } from "./discardPile";
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
	/** Uniform scale for the local hand row and the draw pile beside it. */
	handScale: number;
	/** Scale for the discard pile at the mat's center — the same size as the
	 *  hand's own cards, so the card you're about to play and the card it lands
	 *  on read as the same object. Shrinks below that only when the nearest
	 *  opponent seat is close enough that a full-size pile would collide. */
	centerScale: number;
	/** World X of the discard pile's center. Zero except when the draw pile has
	 *  moved onto the mat beside it, where both slide half a neighbour over so
	 *  the PAIR is centered rather than the discard alone. */
	discardX: number;
	/** World Z of the discard pile's center. The world origin on a wide screen;
	 *  on a portrait one it drops into the middle of the felt the opponent arch
	 *  leaves free, since there the arch only ever occupies -Z and a pile on the
	 *  origin sits right against its lower edge with the whole near half of the
	 *  table empty below it. */
	discardZ: number;
	/** World Z of the local hand row (and of the draw pile's top card). */
	localSeatZ: number;
	/** World Z of the local player's own avatar, just above the hand row. */
	localAvatarZ: number;
	/** World X of the draw pile's center, left of the hand row. */
	drawPileX: number;
	/** World Z of the draw pile's center. Equal to the hand row's own Z on a
	 *  wide screen, where the pile sits beside the hand; on a narrow one the
	 *  pile moves up onto the mat next to the discard instead. */
	drawPileZ: number;
	/** Scale for the draw pile's cards. Matches the hand on a wide screen; a
	 *  fraction of the discard's size once the pile has moved onto the mat,
	 *  where it is a target to tap rather than a stack to read. */
	drawPileScale: number;
	/** Whether the draw pile shares the hand row (true) or sits on the mat next
	 *  to the discard (false). The hand's own span solver needs to know: only in
	 *  the first case does the pile eat into the row's width. */
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

// The draw pile's resting place beside the hand on a wide screen. On a narrow
// one that X would fall outside the frustum, so it slides inward to sit one
// card-half plus a margin inside the left edge instead.
export const DRAW_PILE_HOME_X = -5.5;
export const DRAW_PILE_EDGE_MARGIN = 0.2;

// On a portrait screen the pile can't stay beside the hand: at hand size it is
// a third of the screen's width, and every unit of it comes straight out of the
// row's span (see LocalHand3D's maxHalfSpanEm), which is what left a phone
// showing three cards. It moves onto the mat next to the discard instead — a
// small tap target beside the discard, the way a physical draw pile is just
// "the other stack of cards" rather than sized to match what's being played.
export const MAT_PILE_SCALE = 0.45;
// Clearance between the two piles' own NOMINAL half-widths — the discard's
// real footprint is bigger than that once its scatter is accounted for (see
// discardFootprintHalfWidth below), so this is on top of that, not instead of
// it. Otherwise the discard's jittered/rotated cards and the draw pile's own
// backs visually collide into one blob instead of reading as two piles.
export const MAT_PILE_GAP = 0.35;

// How much of the frustum's width the hand row may span, and how many cards
// have to fit inside it before the row starts scrolling. Without this the card
// size is solved purely from the height of the strip under the felt, which on a
// tall phone screen is generous enough to produce cards a quarter of the screen
// wide — a readable card that you can only see three of is not a readable hand.
export const HAND_WIDTH_FILL = 0.96;
export const HAND_MIN_VISIBLE_CARDS = 7;

// Portrait draws the felt to an explicit box (see playmat.ts's
// portraitMatPlacement) rather than fitting the sheet, so it needs to know how
// far to hold off the two things bracketing it: the opponent arch's labels at
// the top of the screen, and the local player's own avatar at the bottom.
export const PORTRAIT_MAT_TOP_MARGIN = 1.3;
export const PORTRAIT_MAT_LOCAL_GAP = 0.25;

// Where the pile cluster sits between the arch's lower edge (the world origin,
// where the side seats bottom out) and the felt's own near edge, as a fraction
// of the distance between them.
export const PORTRAIT_PILE_CENTER_BIAS = 0.4;
// Cards overlap at CARD_LINE_SPACING_EM against their own ~5em width; the row's
// total width is therefore one full card plus one spacing per further card.
export const HAND_SPACING_RATIO = 0.8;

/**
 * Worst-case half-width the discard pile's own scattered cards can reach from
 * its center, at a given card scale: a card rotated up to MAX_ROTATION_DEG
 * reaches further sideways than an upright one, on top of its own (unscaled —
 * see discardPile.ts) jitter offset. Used to give the draw pile real
 * clearance instead of gapping off a bare, unrotated card width that the
 * scatter routinely reaches past.
 */
function discardFootprintHalfWidth(scale: number): number {
	const rotationRad = (MAX_ROTATION_DEG * Math.PI) / 180;
	const rotatedHalfWidth =
		(CARD_WIDTH * Math.cos(rotationRad) + CARD_HEIGHT * Math.sin(rotationRad)) / 2;
	return rotatedHalfWidth * scale + MAX_JITTER_EM * EM_TO_WORLD;
}

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
	const drawPileBesideHand = viewport.orientation !== "portrait";
	const drawPileScale = drawPileBesideHand ? handScale : centerScale * MAT_PILE_SCALE;
	const clusterPitch =
		MAT_PILE_GAP + discardFootprintHalfWidth(centerScale) + (CARD_WIDTH * drawPileScale) / 2;
	const drawPileX = drawPileBesideHand
		? Math.max(
				DRAW_PILE_HOME_X,
				-(rig.halfWidth - (CARD_WIDTH * handScale) / 2 - DRAW_PILE_EDGE_MARGIN)
			)
		: -clusterPitch / 2;
	const discardX = drawPileBesideHand ? 0 : clusterPitch / 2;
	// The felt: covering the frustum in landscape, and in portrait filling the
	// band between the opponents' arch and the local player's own avatar, so
	// there is no background showing between the table and either seat.
	const mat = isPortrait
		? portraitMatPlacement(
				2 * rig.halfWidth * PORTRAIT_MAT_WIDTH_FILL,
				rig.centerZ - rig.halfHeight + PORTRAIT_MAT_TOP_MARGIN,
				localAvatarZ - LOCAL_AVATAR_WORLD / 2 - PORTRAIT_MAT_LOCAL_GAP
			)
		: landscapeMatPlacement(rig.halfWidth, rig.halfHeight);

	const discardZ = isPortrait ? mat.bounds.near * PORTRAIT_PILE_CENTER_BIAS : 0;
	// The two piles read as one draw/play cluster in portrait, so the draw pile
	// follows the discard down rather than staying on the origin.
	const drawPileZ = drawPileBesideHand ? localSeatZ : discardZ;

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
