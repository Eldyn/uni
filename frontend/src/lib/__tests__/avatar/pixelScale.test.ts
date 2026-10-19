import { describe, expect, it } from "vitest";
import { fitSpriteSize, wholePixelScale } from "$lib/avatar/pixelScale";

describe("pixelScale", () => {
	it("gives the largest whole scale of the 96 px frame that fits", () => {
		expect(wholePixelScale(0)).toBe(1);
		expect(wholePixelScale(191)).toBe(1);
		expect(wholePixelScale(192)).toBe(2);
		expect(wholePixelScale(400)).toBe(4);
	});

	it("gives a sprite size of 96, 192, 288 or 384", () => {
		expect(fitSpriteSize(50)).toBe(96);
		expect(fitSpriteSize(200)).toBe(192);
		expect(fitSpriteSize(300)).toBe(288);
		expect(fitSpriteSize(1000)).toBe(384);
	});
});
