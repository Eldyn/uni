import { describe, it, expect } from "vitest";
import {
	CHEVRON_ARM_BLOCKS,
	CHEVRON_STROKE_BLOCKS,
	LOOP_DASH_THICKNESS_BLOCKS,
	LOOP_CELL_BLOCKS,
	LOOP_CHEVRON_CLEAR_SLOTS,
	LOOP_DASH_BLOCKS,
	LOOP_IDLE_TONE,
	LOOP_MARCH_BLOCKS_PER_SECOND,
	LOOP_MAX_CORNER_RADIUS_BLOCKS,
	LOOP_MIN_CORNER_RADIUS_BLOCKS,
	LOOP_PEAK_TONE,
	LOOP_SLOTS_PER_CELL,
	LOOP_SLOT_BLOCKS,
	advancePhase,
	glowStepCount,
	glowTone,
	wrapPhase
} from "$components/game/animation/loopPlan";

describe("loopPlan constants", () => {
	it("keeps every corner radius the fit may pick above the chevron's reach (shader correctness)", () => {
		expect(LOOP_MIN_CORNER_RADIUS_BLOCKS).toBeGreaterThan(
			CHEVRON_ARM_BLOCKS + CHEVRON_STROKE_BLOCKS + 1
		);
	});

	it("brightens more at the impulse peak than at rest", () => {
		expect(LOOP_PEAK_TONE).toBeGreaterThan(LOOP_IDLE_TONE);
	});

	it("gives every cell an odd slot count so the chevron has a middle slot", () => {
		expect(LOOP_SLOTS_PER_CELL % 2).toBe(1);
		expect(LOOP_CELL_BLOCKS).toBe(LOOP_SLOT_BLOCKS * LOOP_SLOTS_PER_CELL);
	});

	it("keeps dashes shorter than a slot and clear slots inside the cell", () => {
		expect(LOOP_DASH_BLOCKS).toBeLessThan(LOOP_SLOT_BLOCKS);
		expect(LOOP_CHEVRON_CLEAR_SLOTS).toBeLessThan(Math.floor(LOOP_SLOTS_PER_CELL / 2));
	});

	it("keeps dashes at least as thick as one pixel and no thicker than a chevron stroke", () => {
		expect(LOOP_DASH_THICKNESS_BLOCKS).toBeGreaterThanOrEqual(1);
		expect(LOOP_DASH_THICKNESS_BLOCKS).toBeLessThanOrEqual(CHEVRON_STROKE_BLOCKS);
	});

	it("keeps the radius range ordered", () => {
		expect(LOOP_MIN_CORNER_RADIUS_BLOCKS).toBeLessThan(LOOP_MAX_CORNER_RADIUS_BLOCKS);
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
	it("maps glow 0..1 to idle..peak tone", () => {
		expect(glowTone(0)).toBeCloseTo(LOOP_IDLE_TONE);
		expect(glowTone(1)).toBeCloseTo(LOOP_PEAK_TONE);
	});

	it("quantises the impulse to the 12fps ambient grid with at least one step", () => {
		expect(glowStepCount(0.35)).toBe(4);
		expect(glowStepCount(0.01)).toBe(1);
	});
});
