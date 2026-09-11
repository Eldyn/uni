/** Pure ripple-pool logic for the home screen's shader background — no
 *  Threlte/DOM dependency, just a clock-driven pool that ShaderBackground.svelte
 *  samples every frame and packs into shader uniforms. */

export const MAX_RIPPLES = 2;
export const CENTER_LOOP_SECONDS = 10;
export const EDGE_SPAWN_INTERVAL_SECONDS = 5;

const RIPPLE_LIFETIME_SECONDS = 4;

interface Ripple {
	/** uv-space origin, 0..1 in both axes. */
	x: number;
	y: number;
	/** Seconds on the field's own running clock when this ripple started. */
	startTime: number;
	/** Ring grows from 0 to this radius (uv-space, aspect-corrected) over its life. */
	maxRadius: number;
	/** 0..1, how strongly this ripple's ring dithers in at its peak. */
	maxIntensity: number;
	/** The always-on ripple anchored at the UNI! logo; loops instead of dying. */
	isCenter: boolean;
}

function randomEdgePoint(rng: () => number): { x: number; y: number } {
	const side = Math.floor(rng() * 4);
	const t = rng();
	if (side === 0) return { x: t, y: 0 };
	if (side === 1) return { x: t, y: 1 };
	if (side === 2) return { x: 0, y: t };
	return { x: 1, y: t };
}

export class RippleField {
	private ripples: (Ripple | null)[] = new Array(MAX_RIPPLES).fill(null);
	private readonly centerIndex = 0;
	private nextSpawnTime = 0;
	private readonly rng: () => number;

	constructor(centerX: number, centerY: number, rng: () => number = Math.random) {
		this.rng = rng;
		this.ripples[this.centerIndex] = {
			x: centerX,
			y: centerY,
			startTime: 0,
			maxRadius: 1.4,
			maxIntensity: 0.8,
			isCenter: true
		};
	}

	/** Advances ripple state to `time` (seconds on the field's own clock),
	 *  spawning a new edge ripple and recycling any expired ones. */
	update(time: number): void {
		if (time >= this.nextSpawnTime) {
			this.spawnEdgeRipple(time);
			this.nextSpawnTime = time + EDGE_SPAWN_INTERVAL_SECONDS;
		}
		for (let i = 0; i < this.ripples.length; i++) {
			const r = this.ripples[i];
			if (!r || r.isCenter) continue;
			if (time - r.startTime > RIPPLE_LIFETIME_SECONDS) this.ripples[i] = null;
		}
	}

	private spawnEdgeRipple(time: number): void {
		const slot = this.ripples.findIndex((r, i) => r === null && i !== this.centerIndex);
		if (slot === -1) return; // pool full — sparse by design, just skip this spawn

		// Farthest-of-N candidate sampling instead of a single uniform draw:
		// keeps spawns random rather than gridded, but strongly biases away
		// from wherever a ripple already is so two waves rarely land on top
		// of each other.
		const active = this.ripples.filter((r): r is Ripple => r !== null);
		const CANDIDATE_COUNT = 6;
		let best = randomEdgePoint(this.rng);
		let bestMinDist = -Infinity;
		for (let i = 0; i < CANDIDATE_COUNT; i++) {
			const candidate = randomEdgePoint(this.rng);
			const minDist = active.reduce(
				(min, r) => Math.min(min, Math.hypot(candidate.x - r.x, candidate.y - r.y)),
				Infinity
			);
			if (minDist > bestMinDist) {
				bestMinDist = minDist;
				best = candidate;
			}
		}

		const { x, y } = best;
		this.ripples[slot] = {
			x,
			y,
			startTime: time,
			maxRadius: 0.25 + this.rng() * 0.9, // various levels of propagation
			maxIntensity: 0.4 + this.rng() * 0.6, // various intensities
			isCenter: false
		};
	}

	/** Packs current ripple state into flat arrays ready for shader uniforms. */
	snapshot(time: number): {
		centersX: Float32Array;
		centersY: Float32Array;
		radii: Float32Array;
		intensities: Float32Array;
		shapes: Float32Array;
	} {
		const centersX = new Float32Array(MAX_RIPPLES);
		const centersY = new Float32Array(MAX_RIPPLES);
		const radii = new Float32Array(MAX_RIPPLES);
		const intensities = new Float32Array(MAX_RIPPLES);
		const shapes = new Float32Array(MAX_RIPPLES);

		for (let i = 0; i < MAX_RIPPLES; i++) {
			const r = this.ripples[i];
			if (!r) continue;

			const age = r.isCenter
				? ((time - r.startTime) % CENTER_LOOP_SECONDS) / CENTER_LOOP_SECONDS
				: Math.min(1, (time - r.startTime) / RIPPLE_LIFETIME_SECONDS);

			// Fade in over the first fifth of life, out over the last third — a
			// ring that's neither just born nor about to vanish reads clearest.
			const fade = Math.min(1, age / 0.2) * Math.min(1, (1 - age) / 0.3);

			centersX[i] = r.x;
			centersY[i] = r.y;
			radii[i] = age * r.maxRadius;
			intensities[i] = fade * r.maxIntensity;
			// Pure temporal envelope (no per-ripple maxIntensity scaling),
			// shared with the shader to blow the ring out into a solid ink
			// blot at peak-of-life and pinch it back to a thin line at birth
			// and death — a separate concern from how bright/dense the
			// dithering reads, which is what `intensities` still controls.
			shapes[i] = fade;
		}

		return { centersX, centersY, radii, intensities, shapes };
	}
}
