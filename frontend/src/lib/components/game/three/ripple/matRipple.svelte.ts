/**
 * @file matRipple.svelte.ts
 * @brief Reactive state for the playmat's colour ripple: the colour
 * currently committed to the mat, the colour it's sweeping toward, and the
 * geometry of that sweep. The Playmat3D shader reads these fields as
 * uniforms every frame the ripple is active; the play-beat animation calls
 * `startMatRipple`; the ambient dust reads
 * `originUv`/`startTimeMs`/`durationMs` to derive the ripple front
 * analytically rather than re-deriving it here.
 */
import { storeAnimation } from "$stores/animation.svelte";
import { storeRenderSettings } from "$stores/renderSettings.svelte";
import { rippleDurationMs, type MatUv, type RippleStrength } from "./ripplePlan";

export type { MatUv, RippleStrength };

// Rebeccapurple, matching Playmat3D's own FALLBACK_TINT — the colour shown
// before any real `active_type` has synced in.
const INITIAL_COLOR = "#663399";

export class StoreMatRipple {
	/** Colour the ripple is sweeping away from. */
	fromColor = $state(INITIAL_COLOR);
	/** Colour the ripple is sweeping toward. */
	toColor = $state(INITIAL_COLOR);
	/** Colour actually committed to the mat outside an active ripple —
	 *  `fromColor` while sweeping, `toColor` once it finishes. */
	committedColor = $state(INITIAL_COLOR);
	originUv = $state<MatUv>({ u: 0.5, v: 0.5 });
	startTimeMs = $state(0);
	durationMs = $state(0);
	maxRadius = $state(0);
	strength = $state<RippleStrength>("normal");
	active = $state(false);

	#now: () => number;
	#timer: ReturnType<typeof setTimeout> | null = null;

	constructor(now: () => number = () => performance.now()) {
		this.#now = now;
	}

	/**
	 * Starts a ripple sweeping the mat to `toColor`. When ripples are off
	 * (`storeRenderSettings.matRippleActive` false — the setting, reduced
	 * motion, or animations disabled), the colour change commits instantly
	 * instead. Starting a new ripple while one is already running finishes
	 * the running one immediately first, so the new sweep starts from the
	 * colour actually displayed rather than an old, half-finished target.
	 */
	startMatRipple(
		toColor: string,
		strength: RippleStrength,
		originUv: MatUv,
		maxRadius: number
	): void {
		if (!storeRenderSettings.matRippleActive) {
			this.#clearTimer();
			this.committedColor = toColor;
			this.active = false;
			return;
		}

		if (this.active) this.#finish();

		this.fromColor = this.committedColor;
		this.toColor = toColor;
		this.strength = strength;
		this.originUv = originUv;
		this.maxRadius = maxRadius;
		this.startTimeMs = this.#now();
		this.durationMs = rippleDurationMs(strength, storeAnimation.speedMultiplier);
		this.active = true;

		this.#timer = setTimeout(() => this.#finish(), Math.max(0, this.durationMs));
	}

	/** Idle colour sync (reconnect, spectator join, initial deal) — ignored
	 *  while a ripple is actively sweeping, since it would otherwise stomp
	 *  the in-flight target. */
	syncColor(color: string): void {
		if (this.active) return;
		this.committedColor = color;
	}

	#finish(): void {
		this.#clearTimer();
		this.committedColor = this.toColor;
		this.active = false;
	}

	#clearTimer(): void {
		if (this.#timer !== null) {
			clearTimeout(this.#timer);
			this.#timer = null;
		}
	}
}

export const storeMatRipple = new StoreMatRipple();
