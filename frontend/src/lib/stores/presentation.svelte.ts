/**
 * @file presentation.svelte.ts
 * @brief Persisted presentation/quality settings.
 *
 * `quality` is the tier ceiling the client may use for mod art. `auto` derives
 * the ceiling from the device's WebGL capability and `prefers-reduced-motion`
 * (already tracked live by `storeWebglCapability`), so low-end devices and
 * reduced-motion users never request heavy art. Shaped after `audio.svelte.ts`:
 * plain `$state` fields, a `localStorage` key, and a try/catch JSON parse.
 */

import { storeWebglCapability } from "$stores/webglCapability.svelte";

const SETTINGS_STORAGE_KEY = "uni:presentation:settings";

/** @brief Asset quality tiers, richest first. */
export type QualityTier = "high" | "medium" | "low";

/** @brief The user-facing setting: an explicit tier or `auto`. */
export type QualitySetting = QualityTier | "auto";

interface PresentationSettings {
	quality: QualitySetting;
}

function isQualitySetting(value: unknown): value is QualitySetting {
	return value === "auto" || value === "high" || value === "medium" || value === "low";
}

class StorePresentation {
	quality = $state<QualitySetting>("auto");

	constructor() {
		try {
			const raw = localStorage.getItem(SETTINGS_STORAGE_KEY);
			const parsed = raw ? (JSON.parse(raw) as Partial<PresentationSettings>) : null;
			if (isQualitySetting(parsed?.quality)) this.quality = parsed.quality;
		} catch {
			// INFO: localStorage unavailable or malformed, fall back to `auto`.
		}
	}

	/**
	 * @brief Effective tier ceiling: the explicit choice, or `auto` derived from
	 * device capability and reduced motion.
	 */
	get tier(): QualityTier {
		if (this.quality !== "auto") return this.quality;
		return storeWebglCapability.deviceTier === "high" ? "high" : "low";
	}

	setQuality(quality: QualitySetting): void {
		this.quality = quality;
		try {
			localStorage.setItem(SETTINGS_STORAGE_KEY, JSON.stringify({ quality }));
		} catch {
			// INFO: localStorage unavailable, the setting still lives in memory.
		}
	}
}

export const storePresentation = new StorePresentation();
