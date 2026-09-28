import { describe, it, expect } from "vitest";
import {
	AMBIENT_STEP_FPS,
	frontRadiusAt,
	maxRadiusUv,
	originToMatUv,
	rippleDurationMs,
	rippleStepCount,
	type RippleStrength
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

const STEP_MS = 1000 / AMBIENT_STEP_FPS;
const MIN_RIPPLE_STEPS = 2;

describe("rippleStepCount", () => {
	it("rounds a raw duration up to the next whole step", () => {
		expect(rippleStepCount(STEP_MS * 3.2)).toBe(4);
	});

	it("leaves an already step-aligned duration alone (no float round-trip drift)", () => {
		const aligned = 6 * STEP_MS;
		expect(rippleStepCount(aligned)).toBe(6);
	});

	it("never returns fewer than MIN_RIPPLE_STEPS", () => {
		expect(rippleStepCount(1)).toBe(MIN_RIPPLE_STEPS);
		expect(rippleStepCount(0)).toBe(MIN_RIPPLE_STEPS);
	});
});

describe("rippleDurationMs", () => {
	const speedMultipliers = [0.5, 1, 3];
	const strengths: RippleStrength[] = ["normal", "wild"];

	for (const strength of strengths) {
		for (const speedMultiplier of speedMultipliers) {
			it(`is a step multiple of at least ${MIN_RIPPLE_STEPS} steps for ${strength} at ${speedMultiplier}x`, () => {
				const duration = rippleDurationMs(strength, speedMultiplier);
				const stepsFloat = duration / STEP_MS;
				expect(stepsFloat).toBeCloseTo(Math.round(stepsFloat), 6);
				expect(Math.round(stepsFloat)).toBeGreaterThanOrEqual(MIN_RIPPLE_STEPS);
			});
		}
	}

	it("divides the base duration by the speed multiplier before aligning", () => {
		// 2x speed halves the 450ms base to 225ms, which rounds up to 3 steps.
		expect(rippleDurationMs("normal", 2)).toBeCloseTo(3 * STEP_MS, 6);
	});
});

describe("frontRadiusAt", () => {
	const speedMultipliers = [0.5, 1, 3];
	const strengths: RippleStrength[] = ["normal", "wild"];

	for (const strength of strengths) {
		for (const speedMultiplier of speedMultipliers) {
			it(`reaches maxRadius a step before the aligned ${strength} duration completes at ${speedMultiplier}x`, () => {
				const duration = rippleDurationMs(strength, speedMultiplier);
				const lastStepElapsed = duration - STEP_MS;

				expect(frontRadiusAt(lastStepElapsed, duration, 100)).toBe(100);
				expect(lastStepElapsed).toBeLessThan(duration);
			});
		}
	}

	it("shows a nonzero radius on the very first step", () => {
		const duration = rippleDurationMs("normal", 1);
		expect(frontRadiusAt(0, duration, 100)).toBeGreaterThan(0);
	});

	it("holds the same radius within a single 12fps step", () => {
		const duration = rippleDurationMs("normal", 1);
		const early = frontRadiusAt(STEP_MS * 1.1, duration, 100);
		const late = frontRadiusAt(STEP_MS * 1.9, duration, 100);
		expect(early).toBe(late);
	});

	it("advances to the next step's radius once elapsed crosses it", () => {
		const duration = rippleDurationMs("normal", 1);
		const stepOne = frontRadiusAt(STEP_MS * 1.9, duration, 100);
		const stepTwo = frontRadiusAt(STEP_MS * 2.1, duration, 100);
		expect(stepTwo).toBeGreaterThan(stepOne);
	});

	it("clamps to maxRadius once elapsed passes the duration", () => {
		const duration = rippleDurationMs("normal", 1);
		expect(frontRadiusAt(10_000, duration, 100)).toBe(100);
	});

	it("never goes negative for a negative elapsed time", () => {
		const duration = rippleDurationMs("normal", 1);
		expect(frontRadiusAt(-50, duration, 100)).toBe(0);
	});

	it("returns the full radius when durationMs is zero or negative", () => {
		expect(frontRadiusAt(0, 0, 100)).toBe(100);
		expect(frontRadiusAt(50, -10, 100)).toBe(100);
	});
});
