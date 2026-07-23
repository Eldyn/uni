import { describe, it, expect } from "vitest";

import { computeHandRingSlots } from "$components/game/layout/handRing";

describe("computeHandRingSlots", () => {
	it("returns no slots for zero or negative cards", () => {
		expect(computeHandRingSlots(0)).toEqual([]);
		expect(computeHandRingSlots(-2)).toEqual([]);
	});

	it("places a single card on the center line toward the playmat", () => {
		const [slot] = computeHandRingSlots(1);
		expect(slot.rotateDeg).toBe(0);
		expect(slot.x).toBeCloseTo(0, 5);
		expect(slot.y).toBeGreaterThan(0);
	});

	it("centers the spread so a small hand reads as a symmetric arc", () => {
		const slots = computeHandRingSlots(4);
		const rotations = slots.map((s) => s.rotateDeg);
		// symmetric about 0: first and last are equal-and-opposite
		expect(rotations[0]).toBeCloseTo(-rotations[rotations.length - 1], 5);
		// x offsets mirror across the center line
		expect(slots[0].x).toBeCloseTo(-slots[slots.length - 1].x, 5);
	});

	it("steps adjacent cards by a constant angle before the ring fills", () => {
		const slots = computeHandRingSlots(3);
		const stepA = slots[1].rotateDeg - slots[0].rotateDeg;
		const stepB = slots[2].rotateDeg - slots[1].rotateDeg;
		expect(stepA).toBeCloseTo(stepB, 5);
		expect(stepA).toBeGreaterThan(0);
	});

	it("compresses a large hand into an evenly overlapping full ring", () => {
		const cardCount = 30;
		const slots = computeHandRingSlots(cardCount);
		const step = slots[1].rotateDeg - slots[0].rotateDeg;
		// once wrapped, the step is exactly 360/cardCount, not the flat max
		expect(step).toBeCloseTo(360 / cardCount, 5);
	});

	it("keeps every card at the same radius from the center", () => {
		const slots = computeHandRingSlots(7);
		for (const slot of slots) {
			const radius = Math.hypot(slot.x, slot.y);
			expect(radius).toBeCloseTo(4.5, 5);
		}
	});
});
