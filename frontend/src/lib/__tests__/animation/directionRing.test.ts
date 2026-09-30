import { describe, it, expect } from "vitest";
import { directionSign, detectReverse } from "$components/game/animation/directionRing";

describe("directionSign", () => {
	it("maps positive directions to 1", () => {
		expect(directionSign(1)).toBe(1);
		expect(directionSign(3)).toBe(1);
	});

	it("maps negative and zero directions to -1", () => {
		expect(directionSign(-1)).toBe(-1);
		expect(directionSign(0)).toBe(-1);
	});
});

describe("detectReverse", () => {
	it("is true when the sign flips", () => {
		expect(detectReverse(1, -1)).toBe(true);
		expect(detectReverse(-1, 1)).toBe(true);
	});

	it("is false when the sign holds", () => {
		expect(detectReverse(1, 1)).toBe(false);
		expect(detectReverse(-1, -1)).toBe(false);
	});

	it("reports each of two reverses in a row when fed pairwise", () => {
		const directions = [1, -1, 1];
		const flips = directions.slice(1).map((next, i) => detectReverse(directions[i], next));
		expect(flips).toEqual([true, true]);
	});
});
