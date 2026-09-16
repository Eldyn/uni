import { describe, it, expect } from "vitest";

import {
	pastDragThreshold,
	computeScrollEm,
	findNearestSlotIndex,
	findReorderTargetIndex,
	computeReorderedIds
} from "$components/game/layout/handGesture";

describe("pastDragThreshold", () => {
	it("stays a tap right at the threshold boundary", () => {
		expect(pastDragThreshold(5.999999, 6)).toBe(false);
	});

	it("counts as a drag once it reaches the threshold", () => {
		expect(pastDragThreshold(6, 6)).toBe(true);
		expect(pastDragThreshold(6.000001, 6)).toBe(true);
	});

	it("is symmetric for movement in either direction", () => {
		expect(pastDragThreshold(-10, 6)).toBe(true);
		expect(pastDragThreshold(-2, 6)).toBe(false);
	});
});

describe("computeScrollEm", () => {
	it("scrolls the content opposite the pointer's screen direction (content follows the finger)", () => {
		const startEm = 2;
		const worldPerPixelX = 0.01;
		const handEmToWorld = 0.2;
		const draggedRightPx = 50;
		const result = computeScrollEm(startEm, draggedRightPx, worldPerPixelX, handEmToWorld);
		expect(result).toBeLessThan(startEm);
	});

	it("is the identity when there's no movement", () => {
		expect(computeScrollEm(3.5, 0, 0.01, 0.2)).toBeCloseTo(3.5, 10);
	});
});

describe("findNearestSlotIndex", () => {
	const slots = [{ x: -4 }, { x: -1 }, { x: 2 }, { x: 6 }];

	it("picks the exact slot when the dragged position lands on it", () => {
		expect(findNearestSlotIndex(slots, 2)).toBe(2);
	});

	it("picks the nearer of two slots when a tie is broken by distance", () => {
		expect(findNearestSlotIndex(slots, -2.4)).toBe(1); // closer to -1 than to -4
		expect(findNearestSlotIndex(slots, -2.6)).toBe(0); // closer to -4 than to -1
	});

	it("clamps to an end slot when dragged past every slot", () => {
		expect(findNearestSlotIndex(slots, 100)).toBe(3);
		expect(findNearestSlotIndex(slots, -100)).toBe(0);
	});

	it("returns the only index for a single-card hand", () => {
		expect(findNearestSlotIndex([{ x: 0 }], 5)).toBe(0);
	});
});

describe("computeReorderedIds", () => {
	it("moves an id from the front toward the back", () => {
		expect(computeReorderedIds([1, 2, 3, 4], 0, 2)).toEqual([2, 3, 1, 4]);
	});

	it("moves an id from the back toward the front", () => {
		expect(computeReorderedIds([1, 2, 3, 4], 3, 0)).toEqual([4, 1, 2, 3]);
	});

	it("returns the same array reference when the index doesn't change", () => {
		const orderIds = [1, 2, 3];
		expect(computeReorderedIds(orderIds, 1, 1)).toBe(orderIds);
	});

	it("leaves a single-element list unchanged", () => {
		expect(computeReorderedIds([7], 0, 0)).toEqual([7]);
	});
});

describe("findReorderTargetIndex", () => {
	const slots = [{ x: 0 }, { x: 4 }, { x: 8 }, { x: 12 }];

	it("returns current index when movement is within threshold", () => {
		// Moving right from 0 towards 4: trigger is 0 + 0.8 * 4 = 3.2
		expect(findReorderTargetIndex(slots, 0, 0)).toBe(0);
		expect(findReorderTargetIndex(slots, 0, 2.0)).toBe(0);
		expect(findReorderTargetIndex(slots, 0, 3.19)).toBe(0);
	});

	it("triggers reorder once threshold (80%) is crossed", () => {
		expect(findReorderTargetIndex(slots, 0, 3.2)).toBe(1);
		expect(findReorderTargetIndex(slots, 0, 3.5)).toBe(1);
	});

	it("provides hysteresis preventing early reverse swap", () => {
		// At slot 1 (x = 4), moving left towards slot 0 (x = 0):
		// trigger is 4 - 0.8 * (4 - 0) = 0.8
		// Moving right towards slot 2 (x = 8): trigger is 4 + 0.8 * 4 =
		expect(findReorderTargetIndex(slots, 1, 3.0)).toBe(1);
		expect(findReorderTargetIndex(slots, 1, 1.0)).toBe(1);
		expect(findReorderTargetIndex(slots, 1, 0.8)).toBe(0);
		expect(findReorderTargetIndex(slots, 1, 0.5)).toBe(0);
	});

	it("steps across multiple slots when dragged far", () => {
		expect(findReorderTargetIndex(slots, 0, 10.0)).toBe(2);
		expect(findReorderTargetIndex(slots, 0, 15.0)).toBe(3);
	});

	it("handles boundaries and single slot", () => {
		expect(findReorderTargetIndex([{ x: 0 }], 0, 10)).toBe(0);
		expect(findReorderTargetIndex(slots, -1, 4)).toBe(0);
		expect(findReorderTargetIndex(slots, 10, 4)).toBe(3);
	});
});
