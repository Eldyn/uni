import { describe, it, expect } from "vitest";

import {
	computeSeatPositions3D,
	ringRadiiFor,
	LANDSCAPE_RING_RX,
	LANDSCAPE_RING_RX_MAX_EXTRA,
	LANDSCAPE_RING_RZ,
	LANDSCAPE_RING_RZ_MAX,
	LANDSCAPE_RING_X_EXPONENT,
	PORTRAIT_RING_RX,
	PORTRAIT_RING_RX_MAX_EXTRA,
	PORTRAIT_RING_RZ,
	PORTRAIT_RING_X_EXPONENT,
	type SeatPosition3D
} from "$components/game/layout/seatLayout3D";
import { MAX_OPPONENTS, type ViewportInfo } from "$components/game/layout/seatLayout";
import { computeCameraRig } from "$components/game/layout/cameraRig";
import { computeBoardPlacement } from "$components/game/layout/boardPlacement";
import { computeSceneGeometry } from "$components/game/layout/sceneGeometry";
import { PORTRAIT_FAN_SCALE_SPARSE } from "$components/game/layout/portraitTable";
import { CARD_HEIGHT, CARD_WIDTH } from "$components/game/three/units";
import {
	computeHandRingSlots,
	opponentRingRadiusWorld,
	opponentSeatReachWorld,
	RING_RADIUS_EM,
	ringSlotWorldPose
} from "$components/game/layout/handRing";

const landscape: ViewportInfo = { width: 1200, height: 800, orientation: "landscape" };
const portrait: ViewportInfo = { width: 400, height: 800, orientation: "portrait" };

// Unit vector a seat's local +Z points to after applying rotationY.
function forward({ rotationY }: SeatPosition3D): { x: number; z: number } {
	return { x: Math.sin(rotationY), z: Math.cos(rotationY) };
}

// Inverts the superelliptic X mapping (x = rx·sign(c)·|c|^(2/exp), z = -rz·s)
// back to cos²+sin²; every seat must sit exactly on the ring curve.
function onRing(seat: SeatPosition3D, rx: number, rz: number, xExponent: number): number {
	return Math.abs(seat.x / rx) ** xExponent + (seat.z / rz) ** 2;
}

describe("computeSeatPositions3D", () => {
	it("returns no seats for zero opponents", () => {
		expect(computeSeatPositions3D(0, landscape)).toEqual([]);
	});

	it("places every landscape seat on the arch curve (wide in X, shallow in Z)", () => {
		const seats = computeSeatPositions3D(9, landscape);
		expect(seats).toHaveLength(9);
		expect(LANDSCAPE_RING_RX).toBeGreaterThan(LANDSCAPE_RING_RZ);
		const { rx, rz } = ringRadiiFor(landscape, 9);
		for (const seat of seats) {
			expect(onRing(seat, rx, rz, LANDSCAPE_RING_X_EXPONENT)).toBeCloseTo(1, 5);
		}
	});

	it("widens the landscape ring with opponent count, up to the max extra", () => {
		expect(ringRadiiFor(landscape, 3).rx).toBeCloseTo(LANDSCAPE_RING_RX, 5);
		expect(ringRadiiFor(landscape, MAX_OPPONENTS).rx).toBeCloseTo(
			LANDSCAPE_RING_RX + LANDSCAPE_RING_RX_MAX_EXTRA,
			5
		);
		expect(ringRadiiFor(landscape).rx).toBeCloseTo(
			LANDSCAPE_RING_RX + LANDSCAPE_RING_RX_MAX_EXTRA,
			5
		);
	});

	it("puts a lone opponent on the far side of the table", () => {
		const [seat] = computeSeatPositions3D(1, landscape);
		expect(seat.x).toBeCloseTo(0, 5);
		expect(seat.z).toBeCloseTo(-LANDSCAPE_RING_RZ, 5);
	});

	it("maps the three-player cross to right (+X), far (-Z) and left (-X)", () => {
		const [right, far, left] = computeSeatPositions3D(3, landscape);
		expect(right.x).toBeCloseTo(LANDSCAPE_RING_RX, 5);
		expect(right.z).toBeCloseTo(0, 5);
		expect(far.z).toBeCloseTo(-LANDSCAPE_RING_RZ, 5);
		expect(left.x).toBeCloseTo(-LANDSCAPE_RING_RX, 5);
		expect(left.z).toBeCloseTo(0, 5);
	});

	it("faces every seat toward the playmat center", () => {
		for (const seat of computeSeatPositions3D(6, landscape)) {
			const dist = Math.hypot(seat.x, seat.z);
			const dir = forward(seat);
			expect(dir.x).toBeCloseTo(-seat.x / dist, 5);
			expect(dir.z).toBeCloseTo(-seat.z / dist, 5);
		}
	});

	it("keeps every seat above the local hand row, even at the max table", () => {
		for (const viewport of [landscape, portrait]) {
			const rig = computeCameraRig(viewport, MAX_OPPONENTS);
			const placement = computeBoardPlacement(viewport, rig);
			const handBackZ = placement.localSeatZ - (CARD_HEIGHT * placement.handScale) / 2;
			for (const seat of computeSeatPositions3D(MAX_OPPONENTS, viewport)) {
				expect(seat.z).toBeLessThan(handBackZ);
			}
		}
	});

	// Even spacing in ANGLE bunches seats where the arch turns slowly (its
	// near-vertical sides) — the gap between neighbours there came out roughly
	// half the gap across the top. Seats are spread by arc length instead, so
	// every neighbouring pair sits about the same distance apart.
	it("spaces landscape seats evenly along the arch, not evenly in angle", () => {
		const seats = computeSeatPositions3D(MAX_OPPONENTS, landscape);
		const gaps = seats
			.slice(1)
			.map((seat, i) => Math.hypot(seat.x - seats[i].x, seat.z - seats[i].z));
		expect(Math.max(...gaps) / Math.min(...gaps)).toBeLessThan(1.15);
	});

	// The depth used to be a hard constant, so a squarer window's extra vertical
	// room piled up as empty mat above the top seat while that seat stayed pinned
	// on top of the center pile.
	it("grows the landscape arch's depth into vertical frustum slack", () => {
		const squarish: ViewportInfo = { width: 1400, height: 1050, orientation: "landscape" };
		const rig = computeCameraRig(squarish, 3);
		// What Scene3D passes: the frustum's half-height less the reach a seat's
		// own drawn cards need above it.
		const maxRz = rig.halfHeight - opponentSeatReachWorld(1.5, 0.85);
		const filled = ringRadiiFor(squarish, 3, rig.halfWidth, maxRz);
		expect(filled.rz).toBeGreaterThan(LANDSCAPE_RING_RZ);
		expect(filled.rz).toBeLessThanOrEqual(LANDSCAPE_RING_RZ_MAX);
		// Whatever it grew to, the outermost card still lands inside the frustum.
		expect(filled.rz + opponentSeatReachWorld(1.5, 0.85)).toBeLessThanOrEqual(
			rig.halfHeight + 1e-6
		);
		// The seats it pushes outward move AWAY from the pile, never toward it.
		const base = computeSeatPositions3D(3, squarish, rig.halfWidth);
		const grown = computeSeatPositions3D(3, squarish, rig.halfWidth, maxRz);
		grown.forEach((seat, i) => expect(seat.z).toBeLessThanOrEqual(base[i].z + 1e-6));
	});

	it("never lets the arch's depth run past its cap or below its base", () => {
		for (const halfHeight of [0, 3, 6, 12, 40]) {
			const { rz } = ringRadiiFor(landscape, 8, 12, halfHeight);
			expect(rz).toBeGreaterThanOrEqual(LANDSCAPE_RING_RZ);
			expect(rz).toBeLessThanOrEqual(LANDSCAPE_RING_RZ_MAX);
		}
	});

	it("flips the ring proportions in portrait (narrow in X, deep in Z)", () => {
		expect(PORTRAIT_RING_RZ).toBeGreaterThan(PORTRAIT_RING_RX);
		// Default opponentCount is MAX_OPPONENTS, so this is the fully-widened rx.
		expect(ringRadiiFor(portrait)).toEqual({
			rx: PORTRAIT_RING_RX + PORTRAIT_RING_RX_MAX_EXTRA,
			rz: PORTRAIT_RING_RZ
		});
		for (const seat of computeSeatPositions3D(8, portrait)) {
			const { rx, rz } = ringRadiiFor(portrait, 8);
			expect(onRing(seat, rx, rz, PORTRAIT_RING_X_EXPONENT)).toBeCloseTo(1, 5);
		}
	});

	// The old portrait curve paired a high X exponent (|cos|^0.4) with two
	// gapped rails, so X hugged ±rx almost everywhere and the cap sat empty:
	// the arch read as "∥". A true ∩ has one top-center seat furthest away and
	// both ends curled down the sides, with X sweeping continuously across.
	it("sweeps portrait seats over one continuous ∩ (top-center far, ends curled down)", () => {
		const seats = computeSeatPositions3D(9, portrait);
		const top = seats[Math.floor(seats.length / 2)];
		const rightEnd = seats[0];
		const leftEnd = seats[seats.length - 1];

		// The cap is populated: one seat sits at due-top, furthest from the camera.
		expect(top.x).toBeCloseTo(0, 5);
		expect(top.z).toBeCloseTo(-PORTRAIT_RING_RZ, 5);

		// The ends have curled down the sides — near ±rx in X and pulled back
		// toward the local player in Z, not level with the top seat like a rail.
		expect(rightEnd.x).toBeGreaterThan(PORTRAIT_RING_RX * 0.9);
		expect(leftEnd.x).toBeLessThan(-PORTRAIT_RING_RX * 0.9);
		expect(rightEnd.z).toBeGreaterThan(top.z + 0.5);
		expect(leftEnd.z).toBeGreaterThan(top.z + 0.5);

		// X actually varies across the sweep rather than pinning to ±rx.
		const xs = seats.map((seat) => seat.x);
		expect(Math.max(...xs) - Math.min(...xs)).toBeGreaterThan(PORTRAIT_RING_RX);
	});

	// The real drawn fan reaches opponentSeatReachWorld from the seat centre —
	// the ring radius plus a card's half-diagonal, i.e. measured to a corner,
	// not the edge. Two neighbours' fans stay disjoint when their seats are
	// further apart than the sum of those reaches; portrait's drawn fan scale is
	// capped at the crowded end so this holds all the way to a full table.
	it("keeps portrait seats further apart than the drawn card fans reach", () => {
		for (let opponentCount = 4; opponentCount <= MAX_OPPONENTS; opponentCount++) {
			const geometry = computeSceneGeometry(portrait, opponentCount);
			// The cap only ever shrinks the trial size, never grows it.
			expect(geometry.opponentCardScale).toBeLessThanOrEqual(PORTRAIT_FAN_SCALE_SPARSE + 1e-9);
			expect(geometry.opponentCardScale).toBeGreaterThan(0.15);
			const fanReach = opponentSeatReachWorld(
				geometry.opponentAvatarWorld,
				geometry.opponentCardScale
			);
			const seats = geometry.seats3D;
			for (let i = 1; i < seats.length; i++) {
				const gap = Math.hypot(seats[i].x - seats[i - 1].x, seats[i].z - seats[i - 1].z);
				expect(gap).toBeGreaterThan(2 * fanReach);
			}
		}
	});

	// Concrete worst-case check on the DRAWN geometry, not just seat centres:
	// lay a full ring of cards around each seat and verify no card of one seat
	// reaches any card of the next. A card is treated as a disc of its own
	// half-diagonal, so this is conservative.
	it("draws even a full hand ring clear of the neighbouring seat's cards", () => {
		const MAX_FAN_CARDS = 20; // computeHandRingSlots has closed into a full ring here
		for (const opponentCount of [4, 8, 12, MAX_OPPONENTS]) {
			const geometry = computeSceneGeometry(portrait, opponentCount);
			const ringRadius = opponentRingRadiusWorld(
				geometry.opponentAvatarWorld,
				geometry.opponentCardScale
			);
			const radialScale = ringRadius / RING_RADIUS_EM;
			const cardHalfDiagonal =
				(Math.hypot(CARD_WIDTH, CARD_HEIGHT) * geometry.opponentCardScale) / 2;
			const slots = computeHandRingSlots(MAX_FAN_CARDS);
			const seats = geometry.seats3D;
			for (let i = 1; i < seats.length; i++) {
				const previous = slots.map((slot, c) =>
					ringSlotWorldPose(seats[i - 1], slot, c, radialScale)
				);
				const current = slots.map((slot, c) => ringSlotWorldPose(seats[i], slot, c, radialScale));
				for (const [prevX, , prevZ] of previous) {
					for (const [curX, , curZ] of current) {
						expect(Math.hypot(prevX - curX, prevZ - curZ)).toBeGreaterThanOrEqual(
							2 * cardHalfDiagonal
						);
					}
				}
			}
		}
	});
});
