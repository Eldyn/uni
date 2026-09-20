/**
 * @file audioLogic.ts
 * @brief Pure helper functions for the music/SFX manager: variant/pitch
 * randomization, screen-to-music resolution, and throttle checks. No Web
 * Audio/Howler import, kept fully unit-testable in isolation.
 */

import type { AppScreen } from "$stores/navigation.svelte";
import { SCREEN_MUSIC } from "$data/audioCatalogs";

/**
 * @brief Picks uniformly at random from the given variants.
 * Throws if `variants` is empty, an SFX/track definition with no variants
 * is a data bug, not a runtime condition to swallow silently.
 */
export function pickVariant(variants: string[]): string {
	if (variants.length === 0) throw new Error("pickVariant: variants array is empty");
	const index = Math.floor(Math.random() * variants.length);
	return variants[index];
}

/**
 * @brief Returns a random pitch value within `range` (inclusive-ish, via
 * Math.random() * (max - min) + min). Returns the exact bound when
 * range[0] === range[1] (no jitter).
 */
export function randomPitch(range: [number, number]): number {
	const [min, max] = range;
	if (min === max) return min;
	return Math.random() * (max - min) + min;
}

/**
 * @brief Resolves the music catalog id to play for a given screen.
 * `variant` is accepted for future extensibility (e.g. a future
 * "game:tense" override) but is ignored for this pass.
 */
export function resolveMusicForContext(screen: AppScreen, variant?: string): string | undefined {
	void variant;
	return SCREEN_MUSIC[screen];
}

/**
 * @brief Checks whether playing `id` right now should be throttled.
 * Returns true if `id` was last played (per `lastPlayedAt`) within
 * `def.minIntervalMs` of `now`. Never throttles when `minIntervalMs` is
 * undefined. Pure read-only check, does not mutate `lastPlayedAt`.
 */
export function shouldThrottle(
	id: string,
	lastPlayedAt: Map<string, number>,
	def: { minIntervalMs?: number },
	now: number
): boolean {
	if (def.minIntervalMs === undefined) return false;
	const last = lastPlayedAt.get(id);
	if (last === undefined) return false;
	return now - last < def.minIntervalMs;
}

/**
 * @brief Whether backgrounding should pause audio. Mobile browsers keep
 * rendering a hidden page's AudioContext (and on Android an app switch never
 * unloads it), so mobile must pause. Desktop browsers keep playing the music
 * when the tab is hidden, which is desirable there.
 */
export function isMobileDevice(hints: { userAgent: string; touchPrimary: boolean }): boolean {
	if (hints.touchPrimary) return true;
	return /Android|iPhone|iPad|iPod|Mobile|webOS|BlackBerry|IEMobile|Opera Mini/i.test(
		hints.userAgent
	);
}
