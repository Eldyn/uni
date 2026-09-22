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
	PILE_PEEK_Z,
	MAX_PILE_HEIGHT,
	MAX_DRAW_PILE_STACK,
	computeDrawPileCountAndStep
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

	it("sits at the full stack top when mode is full", () => {
		const { renderedCount, stepY } = computeDrawPileCountAndStep(40, "full");
		const [x, y, z] = drawPileTopPose(placement, 40, "full");
		expect(x).toBe(placement.drawPileX);
		expect(y).toBe(PILE_BASE_HEIGHT + (renderedCount - 1) * stepY);
		expect(z).toBe(
			placement.drawPileZ - (renderedCount - 1) * PILE_PEEK_Z * placement.drawPileScale
		);
	});

	it("offsets the top pose by a signed hover slide along Z", () => {
		const [, , z] = drawPileTopPose(placement, 4, "capped", 0.06);
		expect(z).toBe(placement.drawPileZ - 3 * PILE_PEEK_Z * placement.drawPileScale + 0.06);
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

describe("computeDrawPileCountAndStep", () => {
	describe("capped mode", () => {
		it("clamps renderedCount to [0, MAX_VISIBLE_STACK] and keeps fixed DRAW_PILE_STACK_STEP", () => {
			expect(computeDrawPileCountAndStep(-5, "capped")).toEqual({
				renderedCount: 0,
				stepY: DRAW_PILE_STACK_STEP
			});
			expect(computeDrawPileCountAndStep(0, "capped")).toEqual({
				renderedCount: 0,
				stepY: DRAW_PILE_STACK_STEP
			});
			expect(computeDrawPileCountAndStep(3, "capped")).toEqual({
				renderedCount: 3,
				stepY: DRAW_PILE_STACK_STEP
			});
			expect(computeDrawPileCountAndStep(50, "capped")).toEqual({
				renderedCount: MAX_VISIBLE_STACK,
				stepY: DRAW_PILE_STACK_STEP
			});
		});
	});

	describe("full mode", () => {
		it("handles non-positive and 1-card rawSize gracefully", () => {
			expect(computeDrawPileCountAndStep(0, "full")).toEqual({
				renderedCount: 0,
				stepY: DRAW_PILE_STACK_STEP
			});
			expect(computeDrawPileCountAndStep(-1, "full")).toEqual({
				renderedCount: 0,
				stepY: DRAW_PILE_STACK_STEP
			});
			expect(computeDrawPileCountAndStep(1, "full")).toEqual({
				renderedCount: 1,
				stepY: DRAW_PILE_STACK_STEP
			});
		});

		it("uses standard DRAW_PILE_STACK_STEP when height allows (small pile)", () => {
			const { renderedCount, stepY } = computeDrawPileCountAndStep(10, "full");
			expect(renderedCount).toBe(10);
			// 1.2 / 9 = ~0.133 > 0.02, so clamped to DRAW_PILE_STACK_STEP
			expect(stepY).toBe(DRAW_PILE_STACK_STEP);
		});

		it("compresses step in full mode for large piles and caps rendered count to MAX_DRAW_PILE_STACK (50)", () => {
			const { renderedCount, stepY } = computeDrawPileCountAndStep(100, "full");
			expect(renderedCount).toBe(MAX_DRAW_PILE_STACK);
			expect(stepY).toBeLessThanOrEqual(DRAW_PILE_STACK_STEP);
			expect(stepY).toBeGreaterThanOrEqual(MIN_STACK_STEP);
			expect(stepY).toBeCloseTo(
				Math.min(DRAW_PILE_STACK_STEP, MAX_PILE_HEIGHT / (MAX_DRAW_PILE_STACK - 1))
			);
		});

		it("clamps to max 50 rendered count for huge piles", () => {
			const { renderedCount, stepY } = computeDrawPileCountAndStep(500, "full");
			expect(renderedCount).toBe(MAX_DRAW_PILE_STACK);
			expect(stepY).toBeCloseTo(MAX_PILE_HEIGHT / (MAX_DRAW_PILE_STACK - 1));
		});
	});
});
