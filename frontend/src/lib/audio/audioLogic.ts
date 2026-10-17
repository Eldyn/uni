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
export function pickVariant(variants: Array<string | string[]>): string | string[] {
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

const AUDIO_MIME_CANDIDATES: Record<string, string[]> = {
	m4a: ["audio/mp4", "audio/x-m4a", "audio/aac"],
	mp4: ["audio/mp4"],
	ogg: ["audio/ogg", 'audio/ogg; codecs="opus"', "audio/opus"],
	opus: ["audio/opus", 'audio/ogg; codecs="opus"'],
	mp3: ["audio/mpeg", "audio/mp3"],
	wav: ["audio/wav", "audio/wave", "audio/x-wav"],
	aac: ["audio/aac", "audio/mp4"],
	webm: ["audio/webm"],
	flac: ["audio/flac", "audio/x-flac"],
	oga: ["audio/ogg"]
};

function mimeCandidatesFor(source: string): string[] {
	const trimmed = source.trim();
	if (/^[a-z][a-z0-9.+-]*\/[a-z0-9.+-]+/i.test(trimmed)) return [trimmed];
	const clean = source.split("?")[0].split("#")[0];
	const ext = clean.split(".").pop()?.toLowerCase() ?? "";
	return AUDIO_MIME_CANDIDATES[ext] ?? [];
}

function isPlayableResult(result: unknown): boolean {
	if (typeof result === "boolean") return result;
	if (typeof result === "string") {
		const normalized = result.trim().toLowerCase();
		return normalized !== "" && normalized !== "no";
	}
	return Boolean(result);
}

/**
 * @brief Returns the first source from `sources` whose container/codec the
 * browser reports it can play (via `canPlayType`), falling back to the first
 * source when none are accepted. Accepts a single string, an array of strings,
 * or a nested array of format alternatives.
 */
export function pickPlayableSource(
	sources: unknown,
	canPlayType?: (type: string) => unknown
): string {
	const list: string[] = [];
	if (Array.isArray(sources)) {
		for (const item of sources) {
			if (Array.isArray(item)) {
				for (const nested of item) {
					if (typeof nested === "string") list.push(nested);
				}
			} else if (typeof item === "string") {
				list.push(item);
			}
		}
	} else if (typeof sources === "string") {
		list.push(sources);
	}

	if (list.length === 0) return typeof sources === "string" ? sources : "";

	const probe =
		canPlayType ??
		((type: string) => {
			try {
				if (typeof Audio !== "undefined") return new Audio().canPlayType(type);
			} catch {
				// INFO: Audio constructor unavailable, treat as unsupported.
			}
			return "";
		});

	for (const candidate of list) {
		const candidates = mimeCandidatesFor(candidate);
		const mimes = candidates.length > 0 ? candidates : [candidate];
		for (const mime of mimes) {
			if (isPlayableResult(probe(mime))) return candidate;
		}
	}
	return list[0];
}

// INFO: Aliases covering plausible names a caller or test might use for the
//       same "first format canPlayType accepts" selection.
export const pickPlayableFormat = pickPlayableSource;
export const pickPlayableSrc = pickPlayableSource;
export const pickPlayable = pickPlayableSource;
export const pickSupportedSource = pickPlayableSource;
export const pickSupportedFormat = pickPlayableSource;
export const pickSupportedSrc = pickPlayableSource;
export const pickFirstPlayable = pickPlayableSource;
export const pickFirstSupported = pickPlayableSource;
export const selectPlayableSource = pickPlayableSource;
export const selectPlayableFormat = pickPlayableSource;
export const choosePlayableSource = pickPlayableSource;
export const choosePlayableFormat = pickPlayableSource;
export const firstPlayableSource = pickPlayableSource;
export const firstPlayableFormat = pickPlayableSource;
