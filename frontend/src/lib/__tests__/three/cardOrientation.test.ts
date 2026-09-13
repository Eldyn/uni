import { describe, it, expect } from "vitest";
import { computeValueFlipRad } from "$components/game/three/units";

describe("computeValueFlipRad", () => {
	it("returns 0 when alwaysUprightValues is disabled, regardless of rotation", () => {
		expect(computeValueFlipRad(0, false)).toBe(0);
		expect(computeValueFlipRad(45, false)).toBe(0);
		expect(computeValueFlipRad(90, false)).toBe(0);
		expect(computeValueFlipRad(180, false)).toBe(0);
		expect(computeValueFlipRad(-180, false)).toBe(0);
		expect(computeValueFlipRad(270, false)).toBe(0);
	});

	it("returns 0 when card angle is within 90 degrees of upright", () => {
		expect(computeValueFlipRad(0, true)).toBe(0);
		expect(computeValueFlipRad(45, true)).toBe(0);
		expect(computeValueFlipRad(-45, true)).toBe(0);
		expect(computeValueFlipRad(89, true)).toBe(0);
		expect(computeValueFlipRad(-89, true)).toBe(0);
		expect(computeValueFlipRad(90, true)).toBe(0);
		expect(computeValueFlipRad(-90, true)).toBe(0);
	});

	it("returns Math.PI (180-deg flip) when card angle exceeds 90 degrees from upright", () => {
		expect(computeValueFlipRad(91, true)).toBe(Math.PI);
		expect(computeValueFlipRad(-91, true)).toBe(Math.PI);
		expect(computeValueFlipRad(135, true)).toBe(Math.PI);
		expect(computeValueFlipRad(-135, true)).toBe(Math.PI);
		expect(computeValueFlipRad(180, true)).toBe(Math.PI);
		expect(computeValueFlipRad(-180, true)).toBe(Math.PI);
		expect(computeValueFlipRad(190, true)).toBe(Math.PI);
		expect(computeValueFlipRad(-190, true)).toBe(Math.PI);
	});

	it("handles multi-turn angle wraparound (> 360 degrees)", () => {
		expect(computeValueFlipRad(360, true)).toBe(0);
		expect(computeValueFlipRad(540, true)).toBe(Math.PI); // 540 = 180 + 360
		expect(computeValueFlipRad(-540, true)).toBe(Math.PI);
		expect(computeValueFlipRad(720, true)).toBe(0);
	});
});
