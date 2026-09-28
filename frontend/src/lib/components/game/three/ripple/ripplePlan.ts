/**
 * @file ripplePlan.ts
 * @brief Pure math for the playmat colour ripple: where it starts (in the mat
 * mesh's own UV space), how big it has to grow to cover the felt, and how it
 * steps over time. No Svelte, no three.js — matRipple.svelte.ts builds the
 * reactive state on top of this, the Playmat3D shader consumes the
 * result as uniforms, and the ambient dust reads the same
 * origin/start/speed to derive the ripple front analytically instead of
 * duplicating this logic.
 */

import type { MatPlacement } from "../../layout/playmat";

/** Time-quantization shared by every pixel-look animation on the board (see
 *  global-constraints.md): ripple and dust motion both step at this rate
 *  instead of animating continuously. */
export const AMBIENT_STEP_FPS = 12;

const AMBIENT_STEP_MS = 1000 / AMBIENT_STEP_FPS;

// A sweep needs at least this many steps so the 2-step wild flash (see
// WILD_FLASH_STEPS below) always fits inside it, even at a speed multiplier
// high enough that the raw duration would otherwise round to a single step.
const MIN_RIPPLE_STEPS = 2;

// Floating-point guard: an already step-aligned duration (steps *
// AMBIENT_STEP_MS) can land a hair above the exact multiple once it's
// divided back by AMBIENT_STEP_MS, which would push a bare Math.ceil up by a
// whole spurious step. Subtracting this before ceiling absorbs that without
// affecting any duration that's genuinely a fraction of a step past a
// boundary.
const STEP_ROUNDING_EPSILON = 1e-6;

/** Base sweep durations, before the caller's speed multiplier or step
 *  alignment are applied. */
export const NORMAL_RIPPLE_DURATION_MS = 450;
export const WILD_RIPPLE_DURATION_MS = 350;

/** Dither band width, in shader "blocks" (the pixel grid cells) — how
 *  thick the ripple's leading edge reads before it settles into the mat's
 *  new colour. Wild plays get a wider, choppier band. */
export const NORMAL_BAND_BLOCKS = 2;
export const WILD_BAND_BLOCKS = 5;

/** Wild plays also flash the origin itself, in whole 12fps steps, before the
 *  ripple sweeps out from it. */
export const WILD_FLASH_STEPS = 2;
export const WILD_FLASH_RADIUS_BLOCKS = 2;

export type RippleStrength = "normal" | "wild";

/** A point in the playmat mesh's own UV space, [0,1] on each axis. */
export interface MatUv {
	u: number;
	v: number;
}

/**
 * Converts a discard-pile position in board space (`placement.discardX/Z`
 * from `layout/boardPlacement.ts`) into the playmat mesh's UV space — the
 * same UV the fragment shader reads via `vUv`, spanning the whole sheet
 * the mat texture is painted on, not just its opaque felt box.
 *
 * Mirrors Playmat3D.svelte's own placement exactly, inverted: the mat's
 * `T.Group` sits at `(mat.offsetX, mat.offsetZ)` and turns a quarter about Y
 * when `mat.quarterTurn`, and its plane mesh is laid flat (`rotation.x =
 * -Math.PI / 2`) inside that group. A quarter turn swaps which world axis
 * feeds the mesh's local X (the U axis): with no turn it's world X, turned
 * it's -world Z. The mesh's own flattening then puts world Z (or, turned,
 * world X) onto the plane's local Y, which the V axis reads from the far
 * (v=1) edge inward — matching three.js's default texture flipY.
 */
export function originToMatUv(discardX: number, discardZ: number, mat: MatPlacement): MatUv {
	const dx = discardX - mat.offsetX;
	const dz = discardZ - mat.offsetZ;
	const [localX, localZ] = mat.quarterTurn ? [-dz, dx] : [dx, dz];
	const [width, height] = mat.size;
	const u = 0.5 + localX / width;
	const v = 0.5 - localZ / height;
	return { u, v };
}

/**
 * Farthest mat corner from `originUv`, in the same aspect-corrected UV units
 * the shader measures ripple radii in. The radius is expressed in
 * mat-width (U) units: `aspect` is the mesh-local `size[1] / size[0]` ratio
 * (height over width), used as-is regardless of `quarterTurn` — the mesh's
 * own local size already reflects any rotation, so this function never needs
 * to know about it. Applying `aspect` to the V axis's delta before taking the
 * hypotenuse (`length(vec2(du, dv * aspect))`, exactly mirroring the shader's
 * own distance calculation) makes a radius read off this function trace a
 * circle in world space instead of an ellipse skewed by the mesh's
 * non-square UV domain.
 */
export function maxRadiusUv(originUv: MatUv, aspect: number): number {
	const corners: MatUv[] = [
		{ u: 0, v: 0 },
		{ u: 1, v: 0 },
		{ u: 0, v: 1 },
		{ u: 1, v: 1 }
	];
	let max = 0;
	for (const corner of corners) {
		const du = corner.u - originUv.u;
		const dv = (corner.v - originUv.v) * aspect;
		max = Math.max(max, Math.hypot(du, dv));
	}
	return max;
}

/**
 * How many whole 12fps steps a raw duration needs, at least MIN_RIPPLE_STEPS
 * so the 2-step wild flash always fits inside the sweep. Single source of
 * truth for the step alignment: rippleDurationMs uses it to pick the actual
 * sweep length, and frontRadiusAt uses it (via that same aligned duration)
 * to compute progress, so the two can never disagree about when the sweep
 * ends.
 */
export function rippleStepCount(rawDurationMs: number): number {
	const steps = Math.ceil(rawDurationMs / AMBIENT_STEP_MS - STEP_ROUNDING_EPSILON);
	return Math.max(MIN_RIPPLE_STEPS, steps);
}

/** Sweep duration for `strength`, scaled by the caller's speed multiplier
 *  (e.g. storeAnimation.speedMultiplier) and aligned up to a whole number of
 *  12fps steps (rippleStepCount) so frontRadiusAt's stepped progress can
 *  reach exactly 1 on the sweep's last visible step, before completion
 *  commits the mat to its new colour. */
export function rippleDurationMs(strength: RippleStrength, speedMultiplier: number): number {
	const base = strength === "wild" ? WILD_RIPPLE_DURATION_MS : NORMAL_RIPPLE_DURATION_MS;
	const raw = base / speedMultiplier;
	return rippleStepCount(raw) * AMBIENT_STEP_MS;
}

/**
 * Radius of the ripple's leading edge at `elapsedMs`, stepped down to the
 * pixel-look's 12fps grid (AMBIENT_STEP_FPS) rather than growing
 * continuously. `durationMs` is expected to already be step-aligned (see
 * rippleStepCount) — progress is measured in whole steps completed
 * (floor(elapsedMs / stepMs) + 1) out of the total step count, so the last
 * visible step before completion shows the full radius rather than falling
 * a fraction of a step short of it. `durationMs <= 0` is treated as an
 * already-complete sweep and returns the full radius immediately.
 */
export function frontRadiusAt(elapsedMs: number, durationMs: number, maxRadius: number): number {
	if (durationMs <= 0) return maxRadius;
	if (elapsedMs < 0) return 0;
	const totalSteps = rippleStepCount(durationMs);
	const stepsElapsed = Math.floor(elapsedMs / AMBIENT_STEP_MS) + 1;
	const progress = Math.min(1, stepsElapsed / totalSteps);
	return progress * maxRadius;
}
