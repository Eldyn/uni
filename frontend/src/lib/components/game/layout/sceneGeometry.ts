/**
 * @file sceneGeometry.ts
 * @brief Pure geometry solver for the whole Threlte scene — the camera rig,
 * opponent ring/seat positions, opponent avatar/label/card sizing, and the
 * local player's hand/pile placement — as a single function of (viewport,
 * opponentCount). This is the ONE place that composes cameraRig.ts,
 * seatLayout3D.ts, handRing.ts and boardPlacement.ts, so Scene3D.svelte
 * (which draws the scene) and GameBoard.svelte (which needs the pile
 * positions for its DOM flight anchors) always agree on the numbers —
 * computing this twice independently, with subtly different arguments, is
 * exactly what used to let GameBoard's discard-pile anchor and Scene3D's own
 * camera silently disagree about where the center pile really sits.
 */

import { computeCameraRig, type CameraRig } from "./cameraRig";
import {
	computeBoardPlacement,
	avatarBoxPx,
	LOCAL_AVATAR_WORLD,
	type BoardPlacement
} from "./boardPlacement";
import {
	computeSeatPositions3D,
	portraitFanCardScaleCap,
	ringRadiiFor,
	type SeatPosition3D
} from "./seatLayout3D";
import { opponentSeatReachWorld } from "./handRing";
import { CROSS_OPPONENT_COUNT, MAX_OPPONENTS, type ViewportInfo } from "./seatLayout";

// Opponent avatar/label sizes, in WORLD units, from an empty table down to a
// full 16-seat arch. Landscape's empty-table avatar is a little over one ring
// card tall; portrait's is a bare marker, since there the card fan is the
// seat and the icon only has to say who owns it.
const LANDSCAPE_AVATAR_WORLD = LOCAL_AVATAR_WORLD;
const AVATAR_CROWD_SHRINK = 0.2;
const PORTRAIT_AVATAR_WORLD = 0.78;

// Opponent cards are drawn at a fraction of the size the LAYOUT reserves for
// them: the seats stay where a full-size ring would have put them, so
// shrinking the cards opens up the board instead of pulling every seat in
// behind them. A trial setting — drop it back to 1 to undo.
const OPPONENT_CARD_DRAW_SCALE = 0.5;
const LANDSCAPE_LABEL_WORLD = 0.3;
const LABEL_CROWD_SHRINK = 0.1;
const PORTRAIT_LABEL_WORLD = 0.27;
// Readability floor/ceiling for the name plate once its world size is back in
// CSS terms — a name is text, so past a point it stops scaling with the board.
const MIN_LABEL_EM = 0.55;
const MAX_LABEL_EM = 0.9;
const CSS_PX_PER_EM = 16;

export interface SceneGeometry {
	rig: CameraRig;
	ringRadii: { rx: number; rz: number };
	seats3D: SeatPosition3D[];
	isPortrait: boolean;
	crowdT: number;
	ringWidthBoost: number;
	opponentCardLayoutScale: number;
	opponentCardScale: number;
	opponentAvatarWorld: number;
	opponentLabelWorld: number;
	worldPerPx: number;
	opponentAvatarPx: number;
	localAvatarPx: number;
	opponentLabelEm: number;
	opponentSeatReach: number;
	centerClearanceZ: number;
	placement: BoardPlacement;
}

/**
 * Everything the scene needs to draw the current frame, derived once from
 * the viewport and how many opponents are seated. Scene3D.svelte and
 * GameBoard.svelte both call this with the same two arguments and get
 * identical numbers back — there is no second, independently maintained copy
 * of this math anywhere else.
 */
export function computeSceneGeometry(viewport: ViewportInfo, opponentCount: number): SceneGeometry {
	const rig = computeCameraRig(viewport, opponentCount);

	// The frustum is a "contain" fit of the board's reference extents, so
	// whichever axis the viewport is less constrained on ends up with slack. The
	// ring grows into it rather than leaving it as dead mat past the outermost
	// seat — but only as far as the seats it carries actually fit, which is why
	// maxRz is measured from the drawn sizes below rather than guessed. This
	// uncapped read (no maxRz arg) only measures how much the ring COULD widen.
	const ringWidth = ringRadiiFor(viewport, opponentCount, rig.halfWidth);

	// Opponent presentation shrinks smoothly as the landscape table fills; on
	// portrait the card fans stay full-size (they're the seat's focus) while
	// the avatar and name drop to small markers so the fans stay readable.
	const isPortrait = viewport.orientation === "portrait";
	const crowdT = Math.min(
		1,
		Math.max(0, (opponentCount - CROSS_OPPONENT_COUNT) / (MAX_OPPONENTS - CROSS_OPPONENT_COUNT))
	);

	// How much wider the ring grew to fill the frustum's slack, past its own
	// count-based spread — a wide table has that much more room per seat, so
	// the seats and cards can grow with it instead of staying pinned to the
	// crowd-only size a narrower window would've forced. Capped well under the
	// full ratio so an emptied-out table doesn't blow the avatars up.
	const ringWidthBoost = isPortrait
		? 1
		: Math.min(1.15, ringWidth.rx / ringRadiiFor(viewport, opponentCount).rx);

	// A near-empty landscape table has room to spare, so the seats start large
	// and only shrink as they have to; the crowded end of each ramp is what a
	// full 16-seat arch can actually fit.
	// The size the ring is LAID OUT for; what actually gets drawn is this times
	// OPPONENT_CARD_DRAW_SCALE. Every distance on the board — where a seat sits,
	// how much room the discard pile has — is measured off this one so the trial
	// card size can move without dragging the composition with it. Portrait
	// additionally caps the DRAWN scale (below) so a full table's fans fit.
	const opponentCardLayoutScale = (isPortrait ? 0.5 : 0.85 - 0.4 * crowdT) * ringWidthBoost;
	const desiredCardScale = opponentCardLayoutScale * OPPONENT_CARD_DRAW_SCALE;

	// Avatars and labels are sized in WORLD units and only converted to CSS at
	// the end. Declaring them in pixels is what made the board feel inverted: the
	// camera zooms out as the viewport gets less wide-screen, shrinking every
	// card on the table, while a pixel-sized avatar held its exact on-screen size
	// and so grew relative to everything around it — the opposite of "the icons
	// give way so the player's own cards don't have to".
	// No ringWidthBoost here, unlike the cards: an empty landscape table's
	// opponent icon is meant to be exactly the local player's own icon, and a
	// few percent of ring slack is not a reason for the seat across the table to
	// be drawn bigger than the seat you're sitting in.
	const opponentAvatarWorld = isPortrait
		? PORTRAIT_AVATAR_WORLD
		: LANDSCAPE_AVATAR_WORLD - AVATAR_CROWD_SHRINK * crowdT;

	// What a seat's hand fan is actually DRAWN at. Landscape's wide arch has
	// room for the full trial size at every count; portrait's narrower one does
	// not at the crowded end, so there the fan shrinks just enough that its
	// drawn bounding radius (opponentSeatReachWorld — ring radius plus a card's
	// half-diagonal) fits inside half the gap to the next seat. Without this the
	// neighbours' fans visibly interleave from ~12 opponents up.
	const opponentCardScale = isPortrait
		? portraitFanCardScaleCap(opponentCount, viewport, opponentAvatarWorld, desiredCardScale)
		: desiredCardScale;

	// The name label reads fine well before the avatar does, so it tracks the
	// avatar's world size but is clamped to a readable band in CSS terms — an
	// empty table otherwise turns every name into a headline sized to match the
	// biggest icon, and a zoomed-out one into unreadable specks.
	const opponentLabelWorld =
		(isPortrait ? PORTRAIT_LABEL_WORLD : LANDSCAPE_LABEL_WORLD - LABEL_CROWD_SHRINK * crowdT) *
		ringWidthBoost;

	// How far a seat's outermost card sits from the seat itself — the margin the
	// ring needs above it before the top edge, and equally the room the discard
	// pile has below it before the nearest seat.
	const opponentSeatReach = opponentSeatReachWorld(opponentAvatarWorld, opponentCardLayoutScale);
	// Measured from the FAR edge, which portrait's near-biased camera moves in
	// (see cameraRig.ts's centerZ) — the ring keeps its own headroom either way.
	const maxRingRz = rig.halfHeight - rig.centerZ - opponentSeatReach;
	const ringRadii = ringRadiiFor(viewport, opponentCount, rig.halfWidth, maxRingRz);
	const seats3D = computeSeatPositions3D(opponentCount, viewport, rig.halfWidth, maxRingRz);

	// World units per CSS pixel at the current camera zoom — the single
	// conversion every DOM-sized piece of a seat goes through.
	const worldPerPx = (2 * rig.halfWidth) / viewport.width;
	const opponentAvatarPx = avatarBoxPx(opponentAvatarWorld, worldPerPx);
	const localAvatarPx = avatarBoxPx(LOCAL_AVATAR_WORLD, worldPerPx);
	const opponentLabelEm = Math.min(
		MAX_LABEL_EM,
		Math.max(MIN_LABEL_EM, opponentLabelWorld / worldPerPx / CSS_PX_PER_EM)
	);

	const centerClearanceZ = ringRadii.rz - opponentSeatReach;
	const placement = computeBoardPlacement(viewport, rig, centerClearanceZ);

	return {
		rig,
		ringRadii,
		seats3D,
		isPortrait,
		crowdT,
		ringWidthBoost,
		opponentCardLayoutScale,
		opponentCardScale,
		opponentAvatarWorld,
		opponentLabelWorld,
		worldPerPx,
		opponentAvatarPx,
		localAvatarPx,
		opponentLabelEm,
		opponentSeatReach,
		centerClearanceZ,
		placement
	};
}
