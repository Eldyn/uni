import { describe, it, expect } from "vitest";

import {
	computeSeatPositions3D,
	SEAT_RING_RADIUS,
	type SeatPosition3D
} from "$components/game/layout/seatLayout3D";
import type { ViewportInfo } from "$components/game/layout/seatLayout";

const landscape: ViewportInfo = { width: 1200, height: 800, orientation: "landscape" };
const portrait: ViewportInfo = { width: 400, height: 800, orientation: "portrait" };

// Unit vector a seat's local +Z points to after applying rotationY.
function forward({ rotationY }: SeatPosition3D): { x: number; z: number } {
	return { x: Math.sin(rotationY), z: Math.cos(rotationY) };
}

describe("computeSeatPositions3D", () => {
	it("returns no seats for zero opponents", () => {
		expect(computeSeatPositions3D(0, landscape)).toEqual([]);
	});

	it("places every seat on a circle of equal radius (not an ellipse)", () => {
		const seats = computeSeatPositions3D(9, landscape);
		expect(seats).toHaveLength(9);
		for (const seat of seats) {
			expect(Math.hypot(seat.x, seat.z)).toBeCloseTo(SEAT_RING_RADIUS, 5);
		}
	});

	it("puts a lone opponent on the far side of the table", () => {
		const [seat] = computeSeatPositions3D(1, landscape);
		expect(seat.x).toBeCloseTo(0, 5);
		expect(seat.z).toBeCloseTo(-SEAT_RING_RADIUS, 5);
	});

	it("maps the three-player cross to right (+X), far (-Z) and left (-X)", () => {
		const [right, far, left] = computeSeatPositions3D(3, landscape);
		expect(right.x).toBeGreaterThan(0);
		expect(right.z).toBeCloseTo(0, 5);
		expect(far.z).toBeCloseTo(-SEAT_RING_RADIUS, 5);
		expect(left.x).toBeLessThan(0);
		expect(left.z).toBeCloseTo(0, 5);
	});

	it("faces every seat toward the playmat center", () => {
		for (const seat of computeSeatPositions3D(6, landscape)) {
			const dir = forward(seat);
			const toCenter = { x: -seat.x / SEAT_RING_RADIUS, z: -seat.z / SEAT_RING_RADIUS };
			expect(dir.x).toBeCloseTo(toCenter.x, 5);
			expect(dir.z).toBeCloseTo(toCenter.z, 5);
		}
	});

	it("keeps the ring circular in portrait too (framing is camera-side)", () => {
		for (const seat of computeSeatPositions3D(8, portrait)) {
			expect(Math.hypot(seat.x, seat.z)).toBeCloseTo(SEAT_RING_RADIUS, 5);
		}
	});
});
