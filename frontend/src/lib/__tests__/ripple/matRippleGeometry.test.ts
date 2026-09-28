import { describe, it, expect } from "vitest";
import {
	matUvToWorld,
	rippleFrontSpeedWorldPerMs,
	worldUnitsPerUv
} from "$components/game/three/ripple/matRippleGeometry";
import { originToMatUv } from "$components/game/three/ripple/ripplePlan";
import type { MatPlacement } from "$components/game/layout/playmat";

function mat(overrides: Partial<MatPlacement> = {}): MatPlacement {
	return {
		size: [10, 6],
		offsetX: 0,
		offsetZ: 0,
		quarterTurn: false,
		bounds: { left: -5, right: 5, far: -3, near: 3 },
		...overrides
	};
}

describe("matUvToWorld", () => {
	it("puts uv (0.5, 0.5) back at the mat's own center", () => {
		expect(matUvToWorld({ u: 0.5, v: 0.5 }, mat())).toEqual([0, 0]);
	});

	it("round-trips with originToMatUv, unrotated", () => {
		const m = mat();
		const world: [number, number] = [2, -1];
		const uv = originToMatUv(world[0], world[1], m);
		const [x, z] = matUvToWorld(uv, m);
		expect(x).toBeCloseTo(world[0], 10);
		expect(z).toBeCloseTo(world[1], 10);
	});

	it("round-trips with originToMatUv, offset and quarter-turned", () => {
		const m = mat({ offsetX: 4, offsetZ: -2, quarterTurn: true });
		const world: [number, number] = [5, 1.5];
		const uv = originToMatUv(world[0], world[1], m);
		const [x, z] = matUvToWorld(uv, m);
		expect(x).toBeCloseTo(world[0], 10);
		expect(z).toBeCloseTo(world[1], 10);
	});

	it("honors a non-zero mat offset directly", () => {
		expect(matUvToWorld({ u: 0.5, v: 0.5 }, mat({ offsetX: 3, offsetZ: -7 }))).toEqual([3, -7]);
	});
});

describe("worldUnitsPerUv", () => {
	it("equals the mat's own width — one full U unit spans the sheet's width", () => {
		expect(worldUnitsPerUv(mat({ size: [12, 8] }))).toBe(12);
	});
});

describe("rippleFrontSpeedWorldPerMs", () => {
	it("converts a UV radius over a duration into world units per ms", () => {
		// radius 0.5 UV units on a 10-wide mat = 5 world units, over 250ms.
		expect(rippleFrontSpeedWorldPerMs(0.5, 250, mat({ size: [10, 6] }))).toBeCloseTo(0.02, 10);
	});

	it("returns 0 for a non-positive duration instead of dividing by zero", () => {
		expect(rippleFrontSpeedWorldPerMs(0.5, 0, mat())).toBe(0);
		expect(rippleFrontSpeedWorldPerMs(0.5, -10, mat())).toBe(0);
	});
});
