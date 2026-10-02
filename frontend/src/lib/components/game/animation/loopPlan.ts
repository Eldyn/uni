/**
 * @file loopPlan.ts
 * @brief Constants and pure helpers for the play-direction loop on the felt:
 * its size in the mat's art-pixel blocks, how fast the pattern marches, and
 * the brighten impulse a reverse plays. Sizes are starting values tuned by
 * eye.
 */

import { AMBIENT_STEP_FPS } from "../three/ripple/ripplePlan";

/** Distance from the felt's opaque edge to the loop's outline, in blocks. */
export const LOOP_INSET_BLOCKS = 14;
/** Clear space kept between the piles' scatter and the loop on a phone, in blocks. */
export const LOOP_PILE_CLEARANCE_BLOCKS = 12;
/** Preferred corner radius; the fit may nudge it to make the pattern tile. */
export const LOOP_CORNER_RADIUS_BLOCKS = 12;
export const LOOP_MIN_CORNER_RADIUS_BLOCKS = 10;
export const LOOP_MAX_CORNER_RADIUS_BLOCKS = 14;

/** Dashes sit on slots of this many blocks, so their spacing is one whole
 *  number of pixels everywhere along a straight edge. */
export const LOOP_SLOT_BLOCKS = 5;
export const LOOP_DASH_BLOCKS = 3;
export const LOOP_DASH_THICKNESS_BLOCKS = 2;
/** One chevron per cell, in the middle slot. Odd, so that slot has a centre. */
export const LOOP_SLOTS_PER_CELL = 7;
/** Slots either side of a chevron's own slot that carry no dash. */
export const LOOP_CHEVRON_CLEAR_SLOTS = 1;
export const LOOP_CELL_BLOCKS = LOOP_SLOT_BLOCKS * LOOP_SLOTS_PER_CELL;

/** A chevron's arms reach this many blocks to each side of its heading. */
export const CHEVRON_ARM_BLOCKS = 5;
export const CHEVRON_STROKE_BLOCKS = 2;

export const LOOP_MARCH_BLOCKS_PER_SECOND = 6;

/** Grey the loop is drawn in before the mat's colour multiplies it. Above 1 so
 *  it stays a lighter shade of the mat, as the chalk loop was. */
export const LOOP_IDLE_TONE = 1.3;
/** Tone at the peak of a reverse's brighten impulse. */
export const LOOP_PEAK_TONE = 2.2;
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

/** Tone for a glow level in 0..1. */
export function glowTone(glow: number): number {
	return LOOP_IDLE_TONE + (LOOP_PEAK_TONE - LOOP_IDLE_TONE) * glow;
}

export function glowStepCount(durationSeconds: number): number {
	return Math.max(1, Math.round(durationSeconds * AMBIENT_STEP_FPS));
}
