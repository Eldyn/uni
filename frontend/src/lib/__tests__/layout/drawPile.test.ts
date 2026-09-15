import { describe, it, expect } from "vitest";
import {
	visibleDrawPileStackSize,
	drawPileTopPose,
	drawPileStackZ,
	MAX_VISIBLE_STACK,
	MIN_STACK_STEP,
	DRAW_PILE_BURIED_DEPTH_CAP,
	PILE_BASE_HEIGHT,
	DRAW_PILE_STACK_STEP,
	PILE_PEEK_Z
} from "$components/game/layout/drawPile";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";

const placement: BoardPlacement = {
	mat: {} as never,
	handScale: 1,
	centerScale: 1,
	discardX: 0,
	discardZ: 0,
	localSeatZ: 5,
	localAvatarZ: 6,
	drawPileX: -3,
	drawPileZ: 5,
	drawPileScale: 0.8,
	drawPileBesideHand: true
};

describe("visibleDrawPileStackSize", () => {
	it("clamps to [0, MAX_VISIBLE_STACK]", () => {
		expect(visibleDrawPileStackSize(-1)).toBe(0);
		expect(visibleDrawPileStackSize(0)).toBe(0);
		expect(visibleDrawPileStackSize(3)).toBe(3);
		expect(visibleDrawPileStackSize(999)).toBe(MAX_VISIBLE_STACK);
	});
});

describe("drawPileTopPose", () => {
	it("sits at the visible stack's real top-of-stack Y/Z, not the pile's base", () => {
		const [x, y, z] = drawPileTopPose(placement, 4);
		expect(x).toBe(placement.drawPileX);
		expect(y).toBe(PILE_BASE_HEIGHT + 3 * DRAW_PILE_STACK_STEP);
		expect(z).toBe(placement.drawPileZ - 3 * PILE_PEEK_Z * placement.drawPileScale);
	});

	it("falls back to the pile's base when the pile was empty pre-draw", () => {
		const [, y, z] = drawPileTopPose(placement, 0);
		expect(y).toBe(PILE_BASE_HEIGHT);
		expect(z).toBe(placement.drawPileZ);
	});
});

describe("drawPileStackZ", () => {
	it("respects MIN_STACK_STEP and caps Z stack depth for buried cards beyond 20 items", () => {
		expect(MIN_STACK_STEP).toBe(0.004);
		expect(DRAW_PILE_BURIED_DEPTH_CAP).toBe(20);

		// Single parameter index clamp
		expect(drawPileStackZ(20)).toBeCloseTo(20 * DRAW_PILE_STACK_STEP);
		expect(drawPileStackZ(25)).toBe(drawPileStackZ(20));
		expect(drawPileStackZ(100)).toBe(drawPileStackZ(20));

		// When pile has 30 cards, buried cards share tier 0
		expect(drawPileStackZ(0, 30)).toBe(0);
		expect(drawPileStackZ(9, 30)).toBe(0);
		expect(drawPileStackZ(10, 30)).toBeCloseTo(1 * DRAW_PILE_STACK_STEP);
		expect(drawPileStackZ(29, 30)).toBeCloseTo(20 * DRAW_PILE_STACK_STEP);
	});
});
