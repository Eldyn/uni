import { describe, it, expect } from "vitest";
import {
	visibleDrawPileStackSize,
	drawPileTopPose,
	MAX_VISIBLE_STACK,
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
