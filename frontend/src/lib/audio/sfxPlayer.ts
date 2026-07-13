/**
 * @file sfxPlayer.ts
 * @brief One-shot sound-effect playback via Howler, Minecraft-/playsound
 * style: variant/pitch randomization, per-id throttling and concurrency caps.
 */

import { Howl } from "howler";
import { SFX_CATALOG } from "$data/audioCatalogs";
import { pickVariant, randomPitch, shouldThrottle } from "$lib/audio/audioLogic";

export class SfxPlayer {
	#lastPlayedAt = new Map<string, number>();
	#inFlightCount = new Map<string, number>();
	// Grows with the SFX catalog's unique variant count and is never evicted:
	// fine while the catalog stays small; revisit with an eviction policy if
	// it grows large enough for the held decoded buffers to matter.
	#howlCache = new Map<string, Howl>();
	// Warn once per unknown id, not once per call, placeholder SFX ids are
	// wired at real gameplay trigger points (e.g. every card animation) ahead
	// of SFX_CATALOG having entries for them, so an unthrottled warning would
	// flood the console during normal play.
	#warnedUnknownIds = new Set<string>();

	playSfx(id: string, opts?: { pitch?: number; volume?: number }): void {
		const def = SFX_CATALOG[id];
		if (!def) {
			if (!this.#warnedUnknownIds.has(id)) {
				this.#warnedUnknownIds.add(id);
				console.warn(`SfxPlayer.playSfx: unknown sfx id "${id}" (warning shown once)`);
			}
			return;
		}

		const now = Date.now();
		if (shouldThrottle(id, this.#lastPlayedAt, def, now)) return;

		const inFlight = this.#inFlightCount.get(id) ?? 0;
		if (def.maxConcurrent !== undefined && inFlight >= def.maxConcurrent) return;

		let src: string;
		try {
			src = pickVariant(def.variants);
		} catch (error) {
			console.warn(`SfxPlayer.playSfx: no variants available for "${id}"`, error);
			return;
		}

		const howl = this.#getOrCreateHowl(src);
		const pitch = opts?.pitch ?? randomPitch(def.pitchRange ?? [1, 1]);
		const volume = opts?.volume ?? def.volume ?? 1;

		this.#inFlightCount.set(id, inFlight + 1);
		const soundId = howl.play();
		howl.rate(pitch, soundId);
		howl.volume(volume, soundId);

		// Release the in-flight slot on completion OR failure, without the
		// error listeners, a broken/missing source (404, decode error) never
		// fires "end", permanently leaking one in-flight slot for that id and
		// eventually starving it once maxConcurrent is reached.
		const release = () => {
			const remaining = (this.#inFlightCount.get(id) ?? 1) - 1;
			this.#inFlightCount.set(id, Math.max(0, remaining));
		};
		howl.once("end", release, soundId);
		howl.once("loaderror", release, soundId);
		howl.once("playerror", release, soundId);

		this.#lastPlayedAt.set(id, now);
	}

	#getOrCreateHowl(src: string): Howl {
		const cached = this.#howlCache.get(src);
		if (cached) return cached;
		const howl = new Howl({ src: [src] });
		this.#howlCache.set(src, howl);
		return howl;
	}
}
