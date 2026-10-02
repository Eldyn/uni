/**
 * @file loopPlan.ts
 * @brief Constants and pure helpers for the play-direction loop on the felt:
 * its size in the mat's art-pixel blocks, how fast the pattern marches, and
 * the brighten impulse a reverse plays. Sizes are starting values tuned by
 * eye in the final task.
 */

import { AMBIENT_STEP_FPS } from "../three/ripple/ripplePlan";

/** Distance from the felt's opaque edge to the loop's outline, in blocks. */
export const LOOP_INSET_BLOCKS = 10;
/** Must stay above CHEVRON_HALF_SPREAD_BLOCKS so the shader's nearest-point is unambiguous. */
export const LOOP_CORNER_RADIUS_BLOCKS = 10;
/** One chevron per cell; the shader fits the pitch so cells tile the loop. */
export const LOOP_TARGET_PITCH_BLOCKS = 24;

export const CHEVRON_LENGTH_BLOCKS = 4;
export const CHEVRON_HALF_SPREAD_BLOCKS = 3;
export const CHEVRON_STROKE_BLOCKS = 1;

export const DASH_LENGTH_BLOCKS = 2;
export const DASH_PITCH_BLOCKS = 4;
export const DASH_HALF_WIDTH_BLOCKS = 0.5;
export const DASH_CLEARANCE_BLOCKS = 4;

export const LOOP_MARCH_BLOCKS_PER_SECOND = 6;

/** Mix toward white that keeps the loop readable on a felt of the same colour. */
export const LOOP_IDLE_LIGHTEN = 0.35;
export const LOOP_PEAK_LIGHTEN = 0.8;
export const LOOP_FLIP_SECONDS = 0.35;

export function wrapPhase(phase: number, length: number): number {
	return ((phase % length) + length) % length;
}

/** Next pattern offset along the loop. `direction` is +1 or -1. */
export function advancePhase(
	phase: number,
	deltaSeconds: number,
	direction: number,
	speedMultiplier: number,
	loopLength: number
): number {
	const travelled = direction * LOOP_MARCH_BLOCKS_PER_SECOND * speedMultiplier * deltaSeconds;
	return wrapPhase(phase + travelled, loopLength);
}

/** Lighten amount for a glow level in 0..1. */
export function glowLighten(glow: number): number {
	return LOOP_IDLE_LIGHTEN + (LOOP_PEAK_LIGHTEN - LOOP_IDLE_LIGHTEN) * glow;
}

export function glowStepCount(durationSeconds: number): number {
	return Math.max(1, Math.round(durationSeconds * AMBIENT_STEP_FPS));
}
