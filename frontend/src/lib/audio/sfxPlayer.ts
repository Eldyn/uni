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
	#howlCache = new Map<string, Howl>();

	playSfx(id: string, opts?: { pitch?: number; volume?: number }): void {
		const def = SFX_CATALOG[id];
		if (!def) {
			console.warn(`SfxPlayer.playSfx: unknown sfx id "${id}"`);
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
		howl.once(
			"end",
			() => {
				const remaining = (this.#inFlightCount.get(id) ?? 1) - 1;
				this.#inFlightCount.set(id, Math.max(0, remaining));
			},
			soundId
		);

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
