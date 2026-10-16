/**
 * @file cosmeticRng.ts
 * @brief Per-client cosmetic RNG (splitmix64) for mod SFX.
 *
 * Purely presentational randomness — e.g. a `seeded-random-tile` face source —
 * lives here and never enters `storeGame.state`; outcomes stay server RNG. Each
 * instance owns its 64-bit state, so draws are deterministic for a fixed seed
 * and call order and are independent of `Math.random` and any other instance.
 *
 * BigInt arithmetic is plain TS, not WASM.
 */

const MASK64 = (1n << 64n) - 1n;
const SPLITMIX_GAMMA = 0x9e3779b97f4a7c15n;
const FNV_OFFSET = 0xcbf29ce484222325n;
const FNV_PRIME = 0x100000001b3n;

/** FNV-1a 64 over the seed's UTF-8 bytes; deterministic across environments. */
function hashSeed(seed: string): bigint {
	let hash = FNV_OFFSET;
	for (const byte of new TextEncoder().encode(seed)) {
		hash = (hash ^ BigInt(byte)) & MASK64;
		hash = (hash * FNV_PRIME) & MASK64;
	}
	return hash;
}

/** One splitmix64 step: advance the state, return the mixed 64-bit output. */
function splitmix64(state: bigint): { state: bigint; output: bigint } {
	const next = (state + SPLITMIX_GAMMA) & MASK64;
	let z = next;
	z = ((z ^ (z >> 30n)) * 0xbf58476d1ce4e5b9n) & MASK64;
	z = ((z ^ (z >> 27n)) * 0x94d049bb133111ebn) & MASK64;
	z = z ^ (z >> 31n);
	return { state: next, output: z };
}

export class CosmeticRng {
	#state: bigint;

	constructor(seed: string) {
		this.#state = hashSeed(seed);
	}

	/** Uniform integer in `[0, n)`. Degrades to `0` for non-positive `n`. */
	nextInt(n: number): number {
		const step = splitmix64(this.#state);
		this.#state = step.state;
		const bound = Math.floor(n);
		if (!Number.isFinite(bound) || bound <= 1) return 0;
		return Number(step.output % BigInt(bound));
	}

	/** Deterministically pick one element; an empty list yields `undefined`. */
	pick<T>(xs: T[]): T {
		return xs[this.nextInt(xs.length)];
	}
}
