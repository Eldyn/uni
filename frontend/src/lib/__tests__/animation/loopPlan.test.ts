import { describe, it, expect } from "vitest";
import {
	CHEVRON_HALF_SPREAD_BLOCKS,
	LOOP_CORNER_RADIUS_BLOCKS,
	LOOP_IDLE_LIGHTEN,
	LOOP_MARCH_BLOCKS_PER_SECOND,
	LOOP_PEAK_LIGHTEN,
	advancePhase,
	glowLighten,
	glowStepCount,
	wrapPhase
} from "$components/game/animation/loopPlan";

describe("loopPlan constants", () => {
	it("keeps the corner radius above the chevron half-spread (shader correctness)", () => {
		expect(LOOP_CORNER_RADIUS_BLOCKS).toBeGreaterThan(CHEVRON_HALF_SPREAD_BLOCKS);
	});

	it("lightens more at the impulse peak than at rest", () => {
		expect(LOOP_PEAK_LIGHTEN).toBeGreaterThan(LOOP_IDLE_LIGHTEN);
	});
});

describe("wrapPhase", () => {
	it("wraps into [0, length) for positive and negative phases", () => {
		expect(wrapPhase(105, 100)).toBeCloseTo(5);
		expect(wrapPhase(-5, 100)).toBeCloseTo(95);
		expect(wrapPhase(0, 100)).toBe(0);
	});
});

describe("advancePhase", () => {
	it("moves by march speed times delta in the travel direction", () => {
		expect(advancePhase(10, 1, 1, 1, 1000)).toBeCloseTo(10 + LOOP_MARCH_BLOCKS_PER_SECOND);
		expect(advancePhase(10, 1, -1, 1, 1000)).toBeCloseTo(10 - LOOP_MARCH_BLOCKS_PER_SECOND);
	});

	it("scales with the animation speed multiplier", () => {
		expect(advancePhase(0, 1, 1, 2, 1000)).toBeCloseTo(2 * LOOP_MARCH_BLOCKS_PER_SECOND);
	});

	it("wraps past the loop length", () => {
		expect(advancePhase(99, 1, 1, 1, 100)).toBeCloseTo((99 + LOOP_MARCH_BLOCKS_PER_SECOND) % 100);
	});

	it("wraps below zero when marching backwards", () => {
		expect(advancePhase(1, 1, -1, 1, 100)).toBeCloseTo(100 + 1 - LOOP_MARCH_BLOCKS_PER_SECOND);
	});
});

describe("glow helpers", () => {
	it("maps glow 0..1 to idle..peak lighten", () => {
		expect(glowLighten(0)).toBeCloseTo(LOOP_IDLE_LIGHTEN);
		expect(glowLighten(1)).toBeCloseTo(LOOP_PEAK_LIGHTEN);
	});

	it("quantises the impulse to the 12fps ambient grid with at least one step", () => {
		expect(glowStepCount(0.35)).toBe(4);
		expect(glowStepCount(0.01)).toBe(1);
	});
});
