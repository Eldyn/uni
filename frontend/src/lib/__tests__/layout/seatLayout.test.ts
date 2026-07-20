import { describe, it, expect } from "vitest";

import { computeSeatPositions, type ViewportInfo } from "$components/game/layout/seatLayout";

const landscape: ViewportInfo = { width: 1200, height: 800, orientation: "landscape" };
const portrait: ViewportInfo = { width: 400, height: 800, orientation: "portrait" };

describe("computeSeatPositions", () => {
	it("returns no seats for zero opponents", () => {
		expect(computeSeatPositions(0, landscape)).toEqual([]);
	});

	it("places a single opponent at the top in landscape", () => {
		const [seat] = computeSeatPositions(1, landscape);
		expect(seat.isTop).toBe(true);
		expect(seat.xPct).toBeCloseTo(50, 0);
		expect(seat.yPct).toBeLessThan(50);
	});

	it("spreads three opponents with the middle one on top", () => {
		const seats = computeSeatPositions(3, landscape);
		expect(seats).toHaveLength(3);
		expect(seats[1].isTop).toBe(true);
		expect(seats[0].xPct).toBeGreaterThan(seats[1].xPct);
		expect(seats[2].xPct).toBeLessThan(seats[1].xPct);
	});

	it("keeps every seat within the field bounds for large player counts", () => {
		const seats = computeSeatPositions(13, landscape);
		expect(seats).toHaveLength(13);
		for (const seat of seats) {
			expect(seat.xPct).toBeGreaterThanOrEqual(0);
			expect(seat.xPct).toBeLessThanOrEqual(100);
			expect(seat.yPct).toBeGreaterThanOrEqual(0);
			expect(seat.yPct).toBeLessThanOrEqual(100);
		}
	});

	it("shrinks seat scale as the opponent count grows, floored at a minimum", () => {
		const small = computeSeatPositions(3, landscape)[0].scale;
		const large = computeSeatPositions(13, landscape)[0].scale;
		expect(small).toBe(1);
		expect(large).toBeLessThan(small);
		expect(large).toBeGreaterThanOrEqual(0.55);
	});

	it("alternates opponents across left/right rails in portrait, never top", () => {
		const seats = computeSeatPositions(4, portrait);
		expect(seats).toHaveLength(4);
		for (const seat of seats) expect(seat.isTop).toBe(false);
		expect(seats[0].xPct).toBeLessThan(50);
		expect(seats[1].xPct).toBeGreaterThan(50);
		expect(seats[2].xPct).toBeLessThan(50);
		expect(seats[3].xPct).toBeGreaterThan(50);
	});

	it("splits an odd portrait count with the extra seat on the left rail", () => {
		const seats = computeSeatPositions(5, portrait);
		const leftSeats = seats.filter((s) => s.xPct < 50);
		const rightSeats = seats.filter((s) => s.xPct > 50);
		expect(leftSeats).toHaveLength(3);
		expect(rightSeats).toHaveLength(2);
	});
});
