import { describe, expect, it } from "vitest";
import { fitSpriteSize, wholePixelScale } from "$lib/avatar/pixelScale";

describe("pixelScale", () => {
	it("gives the largest whole scale of the 96 px frame that fits", () => {
		expect(wholePixelScale(0)).toBe(1);
		expect(wholePixelScale(191)).toBe(1);
		expect(wholePixelScale(192)).toBe(2);
		expect(wholePixelScale(400)).toBe(4);
	});

	it("gives the largest integer sprite size that fits the box", () => {
		expect(fitSpriteSize(0)).toBe(1);
		expect(fitSpriteSize(47)).toBe(47);
		expect(fitSpriteSize(96)).toBe(96);
		expect(fitSpriteSize(190)).toBe(190);
		expect(fitSpriteSize(500)).toBe(384);
	});

	it("never exceeds a portrait-sized box", () => {
		const heights = [1, 24, 47, 64, 95, 96, 120, 160, 191, 240, 288, 320, 383];

		for (const height of heights) {
			expect(fitSpriteSize(height)).toBeLessThanOrEqual(height);
		}
	});
});
