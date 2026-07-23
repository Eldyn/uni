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

	it("places three opponents in an exact right/top/left cross with the local player", () => {
		const seats = computeSeatPositions(3, landscape);
		expect(seats).toHaveLength(3);
		expect(seats[1].isTop).toBe(true);
		expect(seats[0].yPct).toBeCloseTo(50, 0);
		expect(seats[2].yPct).toBeCloseTo(50, 0);
		expect(seats[0].xPct).toBeGreaterThan(seats[1].xPct);
		expect(seats[2].xPct).toBeLessThan(seats[1].xPct);
	});

	it("keeps the same right/top/left cross for three opponents in portrait", () => {
		const seats = computeSeatPositions(3, portrait);
		expect(seats).toHaveLength(3);
		expect(seats[1].isTop).toBe(true);
		expect(seats[0].xPct).toBeGreaterThan(50);
		expect(seats[2].xPct).toBeLessThan(50);
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
		expect(small).toBe(0.6);
		expect(large).toBeLessThan(small);
		expect(large).toBeGreaterThanOrEqual(0.35);
	});

	it("arcs opponents around a tall ellipse in portrait for more than 4 players", () => {
		const seats = computeSeatPositions(6, portrait);
		expect(seats).toHaveLength(6);
		for (const seat of seats) {
			expect(seat.xPct).toBeGreaterThanOrEqual(0);
			expect(seat.xPct).toBeLessThanOrEqual(100);
			expect(seat.yPct).toBeGreaterThanOrEqual(0);
			expect(seat.yPct).toBeLessThanOrEqual(100);
		}
		expect(seats[0].xPct).toBeGreaterThan(seats[Math.floor(seats.length / 2)].xPct);
	});

	it("keeps every seat within the field bounds for large portrait player counts", () => {
		const seats = computeSeatPositions(13, portrait);
		expect(seats).toHaveLength(13);
		for (const seat of seats) {
			expect(seat.xPct).toBeGreaterThanOrEqual(0);
			expect(seat.xPct).toBeLessThanOrEqual(100);
			expect(seat.yPct).toBeGreaterThanOrEqual(0);
			expect(seat.yPct).toBeLessThanOrEqual(100);
		}
	});

	it("never places a portrait seat near due-top once past the 4-player cross", () => {
		for (const count of [4, 5, 6, 8, 10, 13]) {
			const seats = computeSeatPositions(count, portrait);
			for (const seat of seats) {
				expect(seat.isTop).toBe(false);
			}
		}
	});

	it("splits portrait overflow seats evenly across the right and left rails", () => {
		const seats = computeSeatPositions(6, portrait);
		const onRight = seats.filter((seat) => seat.xPct > 50).length;
		const onLeft = seats.filter((seat) => seat.xPct < 50).length;
		expect(onRight).toBe(3);
		expect(onLeft).toBe(3);
	});

	it("only reads the single seat nearest due-top as flat, not the whole top half of the ring", () => {
		// With 5 opponents the two seats flanking center sit ~41 degrees off
		// due-top; a too-wide top band (the original bug) misreads them as
		// "top" too and leaves their card fans unrotated.
		const seats = computeSeatPositions(5, landscape);
		expect(seats.filter((seat) => seat.isTop)).toHaveLength(1);
	});
});
