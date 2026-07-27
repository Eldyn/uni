import { describe, it, expect } from "vitest";

import { computeHandRingSlots, RING_RADIUS_EM } from "$components/game/layout/handRing";

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

	it("centers a small hand's spread on the line toward the playmat, as a symmetric arc", () => {
		const slots = computeHandRingSlots(4);
		const rotations = slots.map((s) => s.rotateDeg);
		expect(rotations[0]).toBeCloseTo(-rotations[rotations.length - 1], 5);
		expect(slots[0].x).toBeCloseTo(-slots[slots.length - 1].x, 5);
	});

	it("steps adjacent cards by a constant angle while the hand reads as an arc", () => {
		const slots = computeHandRingSlots(5);
		const stepA = slots[1].rotateDeg - slots[0].rotateDeg;
		const stepB = slots[2].rotateDeg - slots[1].rotateDeg;
		expect(stepA).toBeCloseTo(stepB, 5);
		expect(stepA).toBeGreaterThan(0);
		expect(stepA).toBeLessThan(360);
	});

	it("never spans more than a full turn, however many cards", () => {
		for (const cardCount of [2, 5, 12, 20, 40]) {
			const slots = computeHandRingSlots(cardCount);
			const span = slots[slots.length - 1].rotateDeg - slots[0].rotateDeg;
			expect(span).toBeLessThanOrEqual(360 + 1e-6);
		}
	});

	it("closes a large hand into an evenly overlapping full ring", () => {
		const cardCount = 40;
		const slots = computeHandRingSlots(cardCount);
		const step = slots[1].rotateDeg - slots[0].rotateDeg;
		expect(step).toBeCloseTo(360 / cardCount, 5);
	});

	it("closes into a full ring by roughly 20 cards", () => {
		const slots = computeHandRingSlots(20);
		const step = slots[1].rotateDeg - slots[0].rotateDeg;
		expect(step).toBeCloseTo(360 / 20, 1);
	});

	it("keeps every card at the same radius from the center", () => {
		const slots = computeHandRingSlots(7);
		for (const slot of slots) {
			const radius = Math.hypot(slot.x, slot.y);
			expect(radius).toBeCloseTo(RING_RADIUS_EM, 5);
		}
	});
});
