import { describe, it, expect } from "vitest";

import {
	computeHandRingSlots,
	ringSlotWorldPose,
	RING_RADIUS_EM
} from "$components/game/layout/handRing";

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

describe("ringSlotWorldPose", () => {
	it("passes local coordinates straight through for an unrotated seat at the origin", () => {
		const seat = { x: 0, z: 0, rotationY: 0 };
		const slot = { x: 1, y: 2, rotateDeg: 0 };
		const [x, y, z] = ringSlotWorldPose(seat, slot, 3, 1, 0.02);
		expect(x).toBeCloseTo(1);
		expect(y).toBeCloseTo(3 * 0.02);
		expect(z).toBeCloseTo(2);
	});

	it("rotates local (x, y=depth) into world (x, z) by the seat's rotationY, then translates by seat position", () => {
		// A seat rotated 90deg (pi/2): local +Z (slot.y, "toward center") should
		// map to world -X (rotationY = atan2(-x,-z) convention — a seat directly
		// to the table's +X side faces -X, i.e. rotationY = -pi/2 for that seat;
		// this test verifies the rotation math directly against Three's
		// standard Y-rotation matrix: worldX = x*cos + z*sin, worldZ = -x*sin + z*cos).
		const seat = { x: 5, z: -5, rotationY: Math.PI / 2 };
		const slot = { x: 0, y: 1, rotateDeg: 0 };
		const [x, , z] = ringSlotWorldPose(seat, slot, 0, 1);
		// local (lx=0, lz=1) rotated by +90deg: worldX = 0*cos90 + 1*sin90 = 1, worldZ = -0*sin90 + 1*cos90 = 0
		expect(x).toBeCloseTo(5 + 1);
		expect(z).toBeCloseTo(-5 + 0);
	});
});
