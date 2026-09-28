// generateDustSeeds is the only dust code doing randomness: a rejection
// sampler over a spawn box that thins out candidates over the mat footprint.
// A seeded rng keeps this deterministic. The test checks shape and bounds
// only (exact count, every position inside the box), not where particles
// land statistically.

import { describe, it, expect } from "vitest";
import {
	generateDustSeeds,
	DUST_COUNT_HIGH,
	DUST_COUNT_LOW,
	DUST_OVER_MAT_KEEP,
	type DustSpawnBounds,
	type DustMatFootprint
} from "$components/game/three/dust/dustSeeds";

// A small xorshift-style PRNG, seeded, so a test run is reproducible without
// depending on Math.random or vitest's own seeding.
function seededRng(seed: number): () => number {
	let state = seed >>> 0 || 1;
	return () => {
		state ^= state << 13;
		state ^= state >>> 17;
		state ^= state << 5;
		state >>>= 0;
		return state / 0xffffffff;
	};
}

const bounds: DustSpawnBounds = {
	minX: -5,
	maxX: 5,
	minY: -0.1,
	maxY: 3.2,
	minZ: -3,
	maxZ: 3
};

const matRect: DustMatFootprint = { minX: -4, maxX: 4, minZ: -2.5, maxZ: 2.5 };

describe("generateDustSeeds", () => {
	it("returns exactly count instances as a vec4-per-instance Float32Array", () => {
		const count = 24;
		const seeds = generateDustSeeds(count, bounds, matRect, seededRng(1));
		expect(seeds).toBeInstanceOf(Float32Array);
		expect(seeds.length).toBe(count * 4);
	});

	it("keeps every generated position inside the supplied bounds", () => {
		const count = 200;
		const seeds = generateDustSeeds(count, bounds, matRect, seededRng(42));
		for (let i = 0; i < count; i++) {
			const x = seeds[i * 4];
			const y = seeds[i * 4 + 1];
			const z = seeds[i * 4 + 2];
			expect(x).toBeGreaterThanOrEqual(bounds.minX);
			expect(x).toBeLessThanOrEqual(bounds.maxX);
			expect(y).toBeGreaterThanOrEqual(bounds.minY);
			expect(y).toBeLessThanOrEqual(bounds.maxY);
			expect(z).toBeGreaterThanOrEqual(bounds.minZ);
			expect(z).toBeLessThanOrEqual(bounds.maxZ);
		}
	});

	it("writes a fourth phase component within [0, 1) for every instance", () => {
		const count = 50;
		const seeds = generateDustSeeds(count, bounds, matRect, seededRng(7));
		for (let i = 0; i < count; i++) {
			const phase = seeds[i * 4 + 3];
			expect(phase).toBeGreaterThanOrEqual(0);
			expect(phase).toBeLessThan(1);
		}
	});

	it("is deterministic for a given rng sequence", () => {
		const a = generateDustSeeds(30, bounds, matRect, seededRng(99));
		const b = generateDustSeeds(30, bounds, matRect, seededRng(99));
		expect(Array.from(a)).toEqual(Array.from(b));
	});

	it("exposes the named tuning constants", () => {
		expect(DUST_COUNT_HIGH).toBe(160);
		expect(DUST_COUNT_LOW).toBe(60);
		expect(DUST_OVER_MAT_KEEP).toBe(0.25);
	});
});
