import { describe, it, expect } from "vitest";
import { handSlotPose, HAND_STACK_STEP } from "$components/game/layout/handSlotPose";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";

const placement: BoardPlacement = {
	mat: {} as never,
	handScale: 2,
	centerScale: 1,
	discardX: 0,
	discardZ: 0,
	localSeatZ: 5,
	localAvatarZ: 6,
	drawPileX: -3,
	drawPileZ: 5,
	drawPileScale: 1
};

describe("handSlotPose", () => {
	it("stacks resting cards by row index on Y, same as the X solver's own slot", () => {
		const snapshot = { orderIds: [1, 2, 3], scrollEm: 0, maxHalfSpanEm: 100 };
		const [, y0] = handSlotPose(0, 3, snapshot, placement);
		const [, y2] = handSlotPose(2, 3, snapshot, placement);
		expect(y0).toBe(0);
		expect(y2).toBeCloseTo(2 * HAND_STACK_STEP);
	});

	it("jumps to DRAG_LIFT (0.5) instead of the row index while dragging", () => {
		const snapshot = { orderIds: [1, 2, 3], scrollEm: 0, maxHalfSpanEm: 100 };
		const [, y] = handSlotPose(2, 3, snapshot, placement, { dragging: true });
		expect(y).toBe(0.5);
	});

	it("matches computeHandLine's own X for the same slot", () => {
		const snapshot = { orderIds: [10, 20], scrollEm: 0, maxHalfSpanEm: 100 };
		const [x, , z] = handSlotPose(1, 2, snapshot, placement);
		// spacingEm 4 (uncompressed), 2 cards -> slot 1 sits at +2em -> *EM_TO_WORLD(0.2)*handScale(2)
		expect(x).toBeCloseTo(2 * 0.2 * 2);
		expect(z).toBe(placement.localSeatZ);
	});
});
