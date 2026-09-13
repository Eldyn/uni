/**
 * @file animation.svelte.ts
 * @brief Reactive store owning the game board's animation speed and on/off
 * setting. `enabled` defaults to the inverse of the OS-level
 * prefers-reduced-motion signal (storeWebglCapability.reducedMotion) but is
 * independently user-overridable.
 */

import { storeWebglCapability } from "./webglCapability.svelte";

const SETTINGS_STORAGE_KEY = "uni:animation:settings";
const DEFAULT_SPEED_MULTIPLIER = 1;
const MIN_SPEED_MULTIPLIER = 0.5;
const MAX_SPEED_MULTIPLIER = 3;

interface AnimationSettings {
	speedMultiplier: number;
	enabled: boolean;
	alwaysUprightValues: boolean;
}

/** A timeline's timeScale(0) or timeScale(NaN) never reaches progress===1, so
 *  onComplete never fires and CardRegistry wedges forever (same symptom as
 *  an uncaught renderer throw) — clamp on both load and write since
 *  localStorage is user-writable directly, bypassing the Settings slider's
 *  own 50-300 clamp. */
function clampSpeedMultiplier(value: number): number {
	return Number.isFinite(value)
		? Math.min(MAX_SPEED_MULTIPLIER, Math.max(MIN_SPEED_MULTIPLIER, value))
		: DEFAULT_SPEED_MULTIPLIER;
}

class StoreAnimation {
	speedMultiplier = $state<number>(DEFAULT_SPEED_MULTIPLIER);
	enabled = $state<boolean>(!storeWebglCapability.reducedMotion);
	/** Accessibility opt-in: compensates a
	 *  discard card's VALUE layer only, so a thrown card's number always faces
	 *  the local viewer regardless of how the card itself landed. Off by
	 *  default — some players prefer the physically-accurate, sometimes
	 *  upside-down number. */
	alwaysUprightValues = $state<boolean>(false);
	userOverrodeEnabled = false;

	constructor() {
		try {
			const raw = localStorage.getItem(SETTINGS_STORAGE_KEY);
			const parsed = raw ? (JSON.parse(raw) as Partial<AnimationSettings>) : null;
			if (typeof parsed?.speedMultiplier === "number")
				this.speedMultiplier = clampSpeedMultiplier(parsed.speedMultiplier);
			if (typeof parsed?.enabled === "boolean") {
				this.enabled = parsed.enabled;
				this.userOverrodeEnabled = true;
			}
			if (typeof parsed?.alwaysUprightValues === "boolean") {
				this.alwaysUprightValues = parsed.alwaysUprightValues;
			}
		} catch {
			// INFO: localStorage unavailable or malformed, fall back to defaults.
		}

		if (typeof window !== "undefined") {
			window.matchMedia("(prefers-reduced-motion: reduce)").addEventListener("change", (e) => {
				if (!this.userOverrodeEnabled) this.enabled = !e.matches;
			});
		}
	}

	#persist(): void {
		try {
			localStorage.setItem(
				SETTINGS_STORAGE_KEY,
				JSON.stringify({
					speedMultiplier: this.speedMultiplier,
					enabled: this.enabled,
					alwaysUprightValues: this.alwaysUprightValues
				})
			);
		} catch {
			// INFO: localStorage unavailable, setting stays in-memory only.
		}
	}

	setSpeedMultiplier(value: number): void {
		this.speedMultiplier = clampSpeedMultiplier(value);
		this.#persist();
	}

	setEnabled(value: boolean): void {
		this.enabled = value;
		this.userOverrodeEnabled = true;
		this.#persist();
	}

	setAlwaysUprightValues(value: boolean): void {
		this.alwaysUprightValues = value;
		this.#persist();
	}
}

export const storeAnimation = new StoreAnimation();
