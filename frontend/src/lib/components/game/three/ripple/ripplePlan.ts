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

/** Base sweep durations, before the caller's speed multiplier is applied. */
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
 * the shader measures ripple radii in. `aspect` is the mesh's own
 * height/width ratio (`mat.size[1] / mat.size[0]`), applied to the V axis so
 * a radius read off this function traces a circle in world space instead of
 * an ellipse skewed by the mesh's non-square UV domain.
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

/** Sweep duration for `strength`, scaled by the caller's speed multiplier
 *  (e.g. storeAnimation.speedMultiplier). */
export function rippleDurationMs(strength: RippleStrength, speedMultiplier: number): number {
	const base = strength === "wild" ? WILD_RIPPLE_DURATION_MS : NORMAL_RIPPLE_DURATION_MS;
	return base / speedMultiplier;
}

/**
 * Radius of the ripple's leading edge at `elapsedMs`, stepped down to the
 * pixel-look's 12fps grid (AMBIENT_STEP_FPS) rather than growing
 * continuously, then scaled linearly across `durationMs` and clamped to
 * `maxRadius`.
 */
export function frontRadiusAt(elapsedMs: number, durationMs: number, maxRadius: number): number {
	const steppedMs = Math.floor(elapsedMs / AMBIENT_STEP_MS) * AMBIENT_STEP_MS;
	const progress = Math.min(1, Math.max(0, steppedMs / durationMs));
	return progress * maxRadius;
}
