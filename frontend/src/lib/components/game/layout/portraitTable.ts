/**
 * @file portraitTable.ts
 * @brief The phone composition: a tall felt in the middle of the screen with
 * the opponents seated AROUND it, the way the landscape board seats them round
 * the far side of its mat — never on top of the felt itself.
 *
 * Portrait used to reuse the landscape arch with narrower radii, which on a
 * width-bound phone frustum put every seat inside the felt's own edges: the
 * fans sat on the table next to the piles and the felt had to be stretched to
 * twice its drawn depth to fill the screen. Here the felt is solved first as a
 * box inset from the screen edges by exactly one seat's reach, and the seats
 * are then spaced along a rail that runs just outside that box — up the right
 * side, across the top, down the left — so the table and the seats can never
 * overlap by construction.
 *
 * Frame is the same as seatLayout3D.ts: +X table-right, +Z toward the local
 * player, the felt centered on the world origin's X axis.
 */

import { opponentSeatReachWorld } from "./handRing";
import { CROSS_OPPONENT_COUNT, MAX_OPPONENTS } from "./seatLayout";
import type { SeatPosition3D } from "./seatLayout3D";

// A portrait seat's avatar is a small marker next to its fan.
export const PORTRAIT_SEAT_AVATAR_WORLD = 0.78;

// The opponent fan's drawn card scale, from a sparse table to a full one. A
// few opponents can afford a fan big enough to count; a full table's rail
// only has room for a sliver per seat (see the spacing cap below).
export const PORTRAIT_FAN_SCALE_SPARSE = 0.44;
export const PORTRAIT_FAN_SCALE_CROWDED = 0.32;

// Room reserved at the top of the screen for the timer/exit HUD. It is DOM,
// so it's declared in CSS pixels and converted through the camera's scale.
export const PORTRAIT_HUD_CLEARANCE_PX = 80;

// Gap between the screen's side edge and a side seat's outermost card.
export const PORTRAIT_SCREEN_EDGE_MARGIN = 0.1;

// Gap between the felt's painted edge and a seat's nearest card.
export const PORTRAIT_SEAT_FELT_GAP = 0.12;

// A sparse table doesn't spread its few seats over the whole rail — they sit
// this far apart, gathered round the top, and only a crowded table runs the
// full length of both sides.
export const PORTRAIT_SEAT_SPACING = 3;

// Kept between two neighbouring fans' drawn bound circles.
export const PORTRAIT_FAN_CLEARANCE = 0.04;

// Felt width when no seat sits beside it (a lone opponent across the table).
export const PORTRAIT_SEATLESS_FELT_WIDTH_FILL = 0.9;

export interface PortraitTable {
	/** Half the felt's width and depth, and its center's world Z. */
	feltHalfWidth: number;
	feltHalfDepth: number;
	feltCenterZ: number;
	/** Drawn card scale for each opponent fan. */
	fanScale: number;
	/** One seat per opponent, in lap order (see seatLayout.ts). */
	seats: SeatPosition3D[];
}

export interface PortraitFrame {
	halfWidth: number;
	halfHeight: number;
	centerZ: number;
	/** World units per CSS pixel. */
	worldPerPx: number;
}

function crowdingOf(opponentCount: number): number {
	return Math.min(
		1,
		Math.max(0, (opponentCount - CROSS_OPPONENT_COUNT) / (MAX_OPPONENTS - CROSS_OPPONENT_COUNT))
	);
}

function desiredFanScale(opponentCount: number): number {
	const t = crowdingOf(opponentCount);
	return PORTRAIT_FAN_SCALE_SPARSE + (PORTRAIT_FAN_SCALE_CROWDED - PORTRAIT_FAN_SCALE_SPARSE) * t;
}

/** How far a seat's drawn fan reaches past the seat's own center. */
export function portraitSeatReach(opponentCount: number): number {
	return opponentSeatReachWorld(PORTRAIT_SEAT_AVATAR_WORLD, desiredFanScale(opponentCount));
}

// Two players sit across from each other on the sides; everything else has a
// seat on the top edge (a lone opponent, the cross, and every arch).
function hasTopSeat(opponentCount: number): boolean {
	return opponentCount !== 2;
}

function hasSideSeats(opponentCount: number): boolean {
	return opponentCount >= 2;
}

/**
 * The camera's world Z center for a phone, chosen so the felt comes out
 * centered on the world origin: the spectator spin pivots there, and the
 * landscape board keeps its mat there too. Sized for the sparse table's reach
 * (the largest) so a seat joining mid-game never reframes the board.
 *
 * Balances the stack below the felt (the local hand strip) against the stack
 * above it (the HUD and one top seat) by sliding the frame toward whichever is
 * taller. Never past the origin, though: like every rig, a phone only ever
 * biases toward the player, and a narrow one lets the felt sit a touch low.
 */
export function portraitCenterZ(worldPerPx: number, handStripDepth: number): number {
	const reach = portraitSeatReach(1);
	const aboveFelt = PORTRAIT_HUD_CLEARANCE_PX * worldPerPx + PORTRAIT_SEAT_FELT_GAP + 2 * reach;
	return Math.max(0, (handStripDepth - aboveFelt) / 2);
}

/**
 * Walks a rail running `offset` outside a felt box: up the right side from
 * `lowZ`, round the corner, across the top, round the other corner and back
 * down the left side. `at(distance)` returns the point `distance` along it
 * plus the inward normal there, so a seat can face the table square-on.
 */
function feltRail(halfWidth: number, farZ: number, lowZ: number, offset: number) {
	const side = Math.max(0, lowZ - farZ);
	const corner = (Math.PI / 2) * offset;
	const top = 2 * halfWidth;
	const length = 2 * side + 2 * corner + top;

	function at(distance: number): { x: number; z: number; facing: [number, number] } {
		let d = Math.min(length, Math.max(0, distance));
		if (d <= side) return { x: halfWidth + offset, z: lowZ - d, facing: [-1, 0] };
		d -= side;
		if (d <= corner) {
			const angle = d / offset;
			const outX = Math.cos(angle);
			const outZ = -Math.sin(angle);
			return {
				x: halfWidth + offset * outX,
				z: farZ + offset * outZ,
				facing: [-outX, -outZ]
			};
		}
		d -= corner;
		if (d <= top) return { x: halfWidth - d, z: farZ - offset, facing: [0, 1] };
		d -= top;
		if (d <= corner) {
			const angle = Math.PI / 2 + d / offset;
			const outX = Math.cos(angle);
			const outZ = -Math.sin(angle);
			return {
				x: -halfWidth + offset * outX,
				z: farZ + offset * outZ,
				facing: [-outX, -outZ]
			};
		}
		d -= corner;
		return { x: -halfWidth - offset, z: farZ + d, facing: [1, 0] };
	}

	return { length, side, corner, top, at };
}

function seatAt(point: { x: number; z: number; facing: [number, number] }): SeatPosition3D {
	return { x: point.x, z: point.z, rotationY: Math.atan2(point.facing[0], point.facing[1]) };
}

/**
 * Solves the felt box and every seat on it for the given frustum. The felt
 * fills whatever the seats and the local hand strip leave: its sides stop one
 * seat-reach short of the screen edge when there are side seats, its top one
 * seat-reach below the HUD when there is a top seat, and its near edge just
 * above the local player's own avatar.
 */
export function solvePortraitTable(
	frame: PortraitFrame,
	opponentCount: number,
	handStripDepth: number
): PortraitTable {
	const layoutReach = portraitSeatReach(opponentCount);
	const seatOffset = PORTRAIT_SEAT_FELT_GAP + layoutReach;

	const feltHalfWidth = hasSideSeats(opponentCount)
		? frame.halfWidth - PORTRAIT_SCREEN_EDGE_MARGIN - layoutReach - seatOffset
		: frame.halfWidth * PORTRAIT_SEATLESS_FELT_WIDTH_FILL;

	const screenTop = frame.centerZ - frame.halfHeight;
	const screenBottom = frame.centerZ + frame.halfHeight;
	const hudBottom = screenTop + PORTRAIT_HUD_CLEARANCE_PX * frame.worldPerPx;
	const feltFar = hudBottom + (hasTopSeat(opponentCount) ? seatOffset + layoutReach : 0);
	const feltNear = screenBottom - handStripDepth;
	const feltHalfDepth = Math.max(0, (feltNear - feltFar) / 2);
	const feltCenterZ = (feltFar + feltNear) / 2;

	// The side rails stop where a seat's fan would start to dip past the felt's
	// near edge toward the local player's own avatar.
	const railLowZ = feltNear - layoutReach;
	const rail = feltRail(feltHalfWidth, feltFar, railLowZ, seatOffset);
	const midpoint = rail.length / 2;

	let seats: SeatPosition3D[];
	if (opponentCount <= 0) {
		seats = [];
	} else if (opponentCount === 1) {
		seats = [seatAt(rail.at(midpoint))];
	} else if (opponentCount <= CROSS_OPPONENT_COUNT) {
		// The cross: the side seats sit level with the felt's center, the way the
		// landscape cross puts them at due-left/due-right.
		const sideMid = railLowZ - feltCenterZ;
		const right = seatAt(rail.at(sideMid));
		const left = seatAt(rail.at(rail.length - sideMid));
		seats = opponentCount === 2 ? [right, left] : [right, seatAt(rail.at(midpoint)), left];
	} else {
		const spacing = Math.min(PORTRAIT_SEAT_SPACING, rail.length / opponentCount);
		const start = midpoint - (spacing * opponentCount) / 2;
		seats = Array.from({ length: opponentCount }, (_, i) =>
			seatAt(rail.at(start + (i + 0.5) * spacing))
		);
	}

	// A crowded rail can't fit the desired fan: shrink it just enough that two
	// neighbouring fans' drawn bound circles stay apart. Measured seat to seat
	// in a straight line, since round a corner that is shorter than the rail.
	let nearestNeighbour = Infinity;
	for (let i = 1; i < seats.length; i++) {
		const gap = Math.hypot(seats[i].x - seats[i - 1].x, seats[i].z - seats[i - 1].z);
		nearestNeighbour = Math.min(nearestNeighbour, gap);
	}
	const reachAtZero = opponentSeatReachWorld(PORTRAIT_SEAT_AVATAR_WORLD, 0);
	const reachPerScale = opponentSeatReachWorld(PORTRAIT_SEAT_AVATAR_WORLD, 1) - reachAtZero;
	const spacingCap = (nearestNeighbour / 2 - PORTRAIT_FAN_CLEARANCE - reachAtZero) / reachPerScale;
	const fanScale = Math.max(0, Math.min(desiredFanScale(opponentCount), spacingCap));

	return { feltHalfWidth, feltHalfDepth, feltCenterZ, fanScale, seats };
}
