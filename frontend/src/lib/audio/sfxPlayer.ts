/**
 * @file sfxPlayer.ts
 * @brief One-shot sound-effect playback via Howler, Minecraft-/playsound
 * style: variant/pitch randomization, per-id throttling and concurrency caps.
 */

import { Howl } from "howler";
import { SFX_CATALOG } from "$data/audioCatalogs";
import {
	pickVariant,
	playableAudioSource,
	randomPitch,
	shouldThrottle
} from "$lib/audio/audioLogic";

export class SfxPlayer {
	#lastPlayedAt = new Map<string, number>();
	#inFlightCount = new Map<string, number>();
	// Grows with the SFX catalog's unique variant count and is never evicted:
	// fine while the catalog stays small; revisit with an eviction policy if
	// it grows large enough for the held decoded buffers to matter.
	#howlCache = new Map<string, Howl>();
	// One handle per caller-owned loop id; a loop id never holds a one-shot and
	// a one-shot bypasses loop bookkeeping entirely.
	#loops = new Map<string, { howl: Howl; soundId: number }>();
	// Warn once per unknown id, not once per call, placeholder SFX ids are
	// wired at real gameplay trigger points (e.g. every card animation) ahead
	// of SFX_CATALOG having entries for them, so an unthrottled warning would
	// flood the console during normal play.
	#warnedUnknownIds = new Set<string>();

	playSfx(id: string, opts?: { pitch?: number; volume?: number }): void {
		const def = SFX_CATALOG[id];
		if (!def) {
			this.#warnUnknownId(id);
			return;
		}

		const now = Date.now();
		if (shouldThrottle(id, this.#lastPlayedAt, def, now)) return;

		const inFlight = this.#inFlightCount.get(id) ?? 0;
		if (def.maxConcurrent !== undefined && inFlight >= def.maxConcurrent) return;

		let src: string;
		try {
			src = playableAudioSource(pickVariant(def.variants));
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

	/**
	 * @brief Starts (or, under a previously started `loopId`, replaces) a
	 * looping sound instance. `volume` is the absolute Howler volume chosen by
	 * the caller; loops never randomize pitch and never touch the one-shot
	 * throttle/concurrency bookkeeping. Use `stopLoop` with the same `loopId`
	 * to end it.
	 */
	startLoop(loopId: string, sfxId: string, volume: number): void {
		const def = SFX_CATALOG[sfxId];
		if (!def) {
			this.#warnUnknownId(sfxId);
			return;
		}

		let src: string;
		try {
			src = pickVariant(def.variants);
		} catch (error) {
			console.warn(`SfxPlayer.startLoop: no variants available for "${sfxId}"`, error);
			return;
		}

		const howl = this.#getOrCreateHowl(src);
		const soundId = howl.play();
		// INFO: Howler returns -1 when the sound could not start (e.g. locked
		//       context); keep the bookkeeping clean instead of looping a ghost
		//       handle.
		if (typeof soundId !== "number" || soundId < 0) return;

		howl.loop(true, soundId);
		howl.volume(volume, soundId);
		this.#loops.set(loopId, { howl, soundId });
	}

	/** @brief Stops the loop started under `loopId`; unknown ids are a no-op. */
	stopLoop(loopId: string): void {
		const loop = this.#loops.get(loopId);
		if (!loop) return;
		this.#loops.delete(loopId);
		// INFO: Unloop before stopping so the pooled sound slot goes back to the
		//       non-looping behavior its Howl was created for.
		loop.howl.loop(false, loop.soundId);
		loop.howl.stop(loop.soundId);
	}

	/** @brief Retunes the volume of the loop started under `loopId`. */
	setLoopVolume(loopId: string, volume: number): void {
		const loop = this.#loops.get(loopId);
		if (!loop) return;
		loop.howl.volume(volume, loop.soundId);
	}

	#getOrCreateHowl(src: string): Howl {
		const cached = this.#howlCache.get(src);
		if (cached) return cached;
		const howl = new Howl({ src: [src] });
		this.#howlCache.set(src, howl);
		return howl;
	}

	#warnUnknownId(id: string): void {
		if (this.#warnedUnknownIds.has(id)) return;
		this.#warnedUnknownIds.add(id);
		console.warn(`SfxPlayer: unknown sfx id "${id}" (warning shown once)`);
	}
}
