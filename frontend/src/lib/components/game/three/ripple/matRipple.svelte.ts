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
// before any real `active_type` has synced in. Exported so Playmat3D can
// import it in place of that separately-defined FALLBACK_TINT.
export const MAT_INITIAL_COLOR = "#663399";

export class StoreMatRipple {
	/** Colour the ripple is sweeping away from. */
	fromColor = $state(MAT_INITIAL_COLOR);
	/** Colour the ripple is sweeping toward. */
	toColor = $state(MAT_INITIAL_COLOR);
	/** Colour actually committed to the mat outside an active ripple —
	 *  `fromColor` while sweeping, `toColor` once it finishes. */
	committedColor = $state(MAT_INITIAL_COLOR);
	originUv = $state<MatUv>({ u: 0.5, v: 0.5 });
	startTimeMs = $state(0);
	durationMs = $state(0);
	maxRadius = $state(0);
	strength = $state<RippleStrength>("normal");
	active = $state(false);
	/** True between a landing becoming known (`beginPending`) and the card
	 *  actually landing (`startMatRipple`): the mat holds the pre-play colour
	 *  while the snapshot has already moved `active_type` on. */
	pending = $state(false);

	#now: () => number;
	#timer: ReturnType<typeof setTimeout> | null = null;
	#pendingFromColor: string | null = null;

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
			this.clearPending();
			this.committedColor = toColor;
			this.active = false;
			return;
		}

		if (this.active) this.#finish();

		this.fromColor = this.#pendingFromColor ?? this.committedColor;
		this.clearPending();
		this.toColor = toColor;
		this.strength = strength;
		this.originUv = originUv;
		this.maxRadius = maxRadius;
		this.startTimeMs = this.#now();
		this.durationMs = rippleDurationMs(strength, storeAnimation.speedMultiplier);
		this.active = true;

		this.#timer = setTimeout(() => this.#finish(), Math.max(0, this.durationMs));
	}

	/**
	 * Begins the "pending landing" hold: captures the current committed
	 * colour as the from-colour the sweep will use, and blocks `syncColor`
	 * from adopting the new colour the snapshot has already published while
	 * the card is still in flight. A no-op when ripples are inactive, so the
	 * instant-commit path stays instant.
	 */
	beginPending(): void {
		if (!storeRenderSettings.matRippleActive) return;
		// Capture the colour actually displayed: a running sweep finishes
		// here, advancing committedColor to its target, so the next landing
		// sweeps from what the player sees rather than the stale from-colour.
		if (this.active) this.#finish();
		this.#pendingFromColor = this.committedColor;
		this.pending = true;
	}

	/** Drops a hold that will never convert into a landing (match reset). */
	clearPending(): void {
		this.pending = false;
		this.#pendingFromColor = null;
	}

	/** Idle colour sync (reconnect, spectator join, initial deal) — ignored
	 *  while a ripple is actively sweeping or a landing is pending, since it
	 *  would otherwise stomp the in-flight target. */
	syncColor(color: string): void {
		if (this.active || this.pending) return;
		this.committedColor = color;
	}

	/** Drops any running sweep or pending hold and returns the mat to its
	 *  initial colour, e.g. when a match starts or the playmat unmounts. */
	reset(): void {
		this.#clearTimer();
		this.clearPending();
		this.active = false;
		this.fromColor = MAT_INITIAL_COLOR;
		this.toColor = MAT_INITIAL_COLOR;
		this.committedColor = MAT_INITIAL_COLOR;
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
