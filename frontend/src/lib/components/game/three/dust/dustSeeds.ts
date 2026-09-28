/**
 * @file dustSeeds.ts
 * @brief Pure spawn-position generator for AmbientDust3D's instanced motes.
 * Called once per mount (and again whenever the mat's placement changes),
 * to build the `aSeed` vec4-per-instance attribute the dust vertex shader
 * reads: every instance's own spawn (x, y, z) plus a random phase in
 * [0, 1) that offsets its animation cycle so motes don't all breathe in
 * lockstep. All actual motion happens in the shader from this seed plus
 * `uTime` — this module only decides where each mote starts out.
 *
 * No Svelte state, no three.js import — a plain function of its arguments,
 * same shape as matRippleGeometry.ts, so it's cheap to unit test without a
 * WebGL context.
 */

/** Device-tier instance counts, picked by AmbientDust3D from
 *  storeWebglCapability.deviceTier. */
export const DUST_COUNT_HIGH = 160;
export const DUST_COUNT_LOW = 60;

/** A candidate spawn point that falls over the mat's own footprint is kept
 *  only this often — most of the dust reads over the surrounding table
 *  instead of hovering on top of the felt. */
export const DUST_OVER_MAT_KEEP = 0.25;

/** Axis-aligned box motes may spawn anywhere inside, in world units. */
export interface DustSpawnBounds {
	minX: number;
	maxX: number;
	minY: number;
	maxY: number;
	minZ: number;
	maxZ: number;
}

/** Axis-aligned footprint of the mat itself (X/Z only — height doesn't
 *  matter for the over-mat thinning test), in the same world space as
 *  DustSpawnBounds. Matches MatPlacement.bounds's shape. */
export interface DustMatFootprint {
	minX: number;
	maxX: number;
	minZ: number;
	maxZ: number;
}

function lerp(min: number, max: number, t: number): number {
	return min + (max - min) * t;
}

/**
 * Rejection-samples `count` spawn seeds inside `bounds`. A candidate whose
 * X/Z falls inside `matRect` is kept only with probability
 * DUST_OVER_MAT_KEEP (rolled from its own rng draw); everything else is
 * kept outright. Sampling continues until `count` are accepted, so the
 * result is always exactly `count` instances regardless of matRect's size
 * relative to bounds.
 *
 * `rng` defaults to Math.random but is always passed explicitly in tests
 * for reproducibility, and by AmbientDust3D at call time it's the real one.
 */
export function generateDustSeeds(
	count: number,
	bounds: DustSpawnBounds,
	matRect: DustMatFootprint,
	rng: () => number = Math.random
): Float32Array {
	const seeds = new Float32Array(count * 4);
	let accepted = 0;
	while (accepted < count) {
		const x = lerp(bounds.minX, bounds.maxX, rng());
		const y = lerp(bounds.minY, bounds.maxY, rng());
		const z = lerp(bounds.minZ, bounds.maxZ, rng());

		const overMat =
			x >= matRect.minX && x <= matRect.maxX && z >= matRect.minZ && z <= matRect.maxZ;
		if (overMat && rng() >= DUST_OVER_MAT_KEEP) continue;

		const offset = accepted * 4;
		seeds[offset] = x;
		seeds[offset + 1] = y;
		seeds[offset + 2] = z;
		seeds[offset + 3] = rng();
		accepted++;
	}
	return seeds;
}
