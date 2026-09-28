import { describe, it, expect } from "vitest";
import {
	AMBIENT_STEP_FPS,
	NORMAL_RIPPLE_DURATION_MS,
	WILD_RIPPLE_DURATION_MS,
	frontRadiusAt,
	maxRadiusUv,
	originToMatUv,
	rippleDurationMs
} from "$components/game/three/ripple/ripplePlan";
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

describe("originToMatUv", () => {
	it("puts the mat's own center at uv (0.5, 0.5)", () => {
		expect(originToMatUv(0, 0, mat())).toEqual({ u: 0.5, v: 0.5 });
	});

	it("maps +X to a larger u and -Z (far side) to a larger v, unrotated", () => {
		const uv = originToMatUv(2, -1, mat());
		expect(uv.u).toBeCloseTo(0.7, 10);
		expect(uv.v).toBeCloseTo(0.6667, 4);
	});

	it("honors a non-zero mat offset", () => {
		const uv = originToMatUv(3, 2, mat({ offsetX: 1, offsetZ: 2 }));
		// dx = 2, dz = 0 -> same as the unrotated case above but shifted
		expect(uv.u).toBeCloseTo(0.7, 10);
		expect(uv.v).toBeCloseTo(0.5, 10);
	});

	it("swaps axes for a quarter-turned mat", () => {
		const turned = mat({ offsetX: 1, offsetZ: 2, quarterTurn: true });
		// dx = 3, dz = -2 -> localX = -dz = 2, localZ = dx = 3
		const uv = originToMatUv(4, 0, turned);
		expect(uv.u).toBeCloseTo(0.7, 10);
		expect(uv.v).toBeCloseTo(0, 10);
	});
});

describe("maxRadiusUv", () => {
	it("is the same for every corner from dead center at aspect 1", () => {
		const radius = maxRadiusUv({ u: 0.5, v: 0.5 }, 1);
		expect(radius).toBeCloseTo(Math.SQRT1_2, 10);
	});

	it("picks the farthest corner and applies the aspect correction to v", () => {
		const radius = maxRadiusUv({ u: 0.2, v: 0.3 }, 2);
		// farthest corner is (1, 1): du = 0.8, dv = (1 - 0.3) * 2 = 1.4
		expect(radius).toBeCloseTo(Math.hypot(0.8, 1.4), 10);
	});
});

describe("rippleDurationMs", () => {
	it("uses the normal base duration at 1x speed", () => {
		expect(rippleDurationMs("normal", 1)).toBe(NORMAL_RIPPLE_DURATION_MS);
	});

	it("uses the wild base duration at 1x speed", () => {
		expect(rippleDurationMs("wild", 1)).toBe(WILD_RIPPLE_DURATION_MS);
	});

	it("divides the base duration by the speed multiplier", () => {
		expect(rippleDurationMs("normal", 2)).toBe(NORMAL_RIPPLE_DURATION_MS / 2);
	});
});

describe("frontRadiusAt", () => {
	const stepMs = 1000 / AMBIENT_STEP_FPS;

	it("starts at zero", () => {
		expect(frontRadiusAt(0, 450, 100)).toBe(0);
	});

	it("holds the same radius within a single 12fps step", () => {
		const early = frontRadiusAt(stepMs * 1.1, 450, 100);
		const late = frontRadiusAt(stepMs * 1.9, 450, 100);
		expect(early).toBe(late);
	});

	it("advances to the next step's radius once elapsed crosses it", () => {
		const stepOne = frontRadiusAt(stepMs * 1.9, 450, 100);
		const stepTwo = frontRadiusAt(stepMs * 2.1, 450, 100);
		expect(stepTwo).toBeGreaterThan(stepOne);
	});

	it("clamps to maxRadius once elapsed passes the duration", () => {
		expect(frontRadiusAt(10_000, 450, 100)).toBe(100);
	});

	it("never goes negative for a zero or negative elapsed time", () => {
		expect(frontRadiusAt(-50, 450, 100)).toBe(0);
	});
});
