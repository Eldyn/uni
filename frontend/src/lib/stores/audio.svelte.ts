/**
 * @file audio.svelte.ts
 * @brief Reactive store owning music/SFX volume settings, the shared
 * MusicPlayer/SfxPlayer instances, and screen-driven music switching.
 */

import { Howler } from "howler";
import { MusicPlayer } from "$lib/audio/musicPlayer";
import { SfxPlayer } from "$lib/audio/sfxPlayer";
import { resolveMusicForContext } from "$lib/audio/audioLogic";
import { storeNavigation } from "$stores/navigation.svelte";

const SETTINGS_STORAGE_KEY = "uni:audio:settings";
const DEFAULT_MUSIC_VOLUME = 0.5;
const DEFAULT_SFX_VOLUME = 0.5;

interface AudioSettings {
	musicVolume: number;
	sfxVolume: number;
}

class StoreAudio {
	musicVolume = $state<number>(DEFAULT_MUSIC_VOLUME);
	sfxVolume = $state<number>(DEFAULT_SFX_VOLUME);

	#music = new MusicPlayer();
	#sfx = new SfxPlayer();
	#initialized = false;

	constructor() {
		try {
			const raw = localStorage.getItem(SETTINGS_STORAGE_KEY);
			const parsed = raw ? (JSON.parse(raw) as Partial<AudioSettings>) : null;
			if (typeof parsed?.musicVolume === "number") this.musicVolume = parsed.musicVolume;
			if (typeof parsed?.sfxVolume === "number") this.sfxVolume = parsed.sfxVolume;
		} catch {
			// localStorage unavailable or settings payload malformed — use defaults.
		}
	}

	/**
	 * @brief Boots the audio system: applies the persisted master volume,
	 * wires the autoplay-unlock-on-gesture fallback, and starts/keeps
	 * screen-driven music in sync with `storeNavigation`. Idempotent — safe
	 * to call more than once. Degrades to a silent no-op if the underlying
	 * Howler/AudioContext is unavailable in this environment.
	 */
	init(): void {
		if (this.#initialized) return;
		this.#initialized = true;

		try {
			// Also lazily creates Howler.ctx as a side effect (Howler only sets
			// up its AudioContext on first real use), which is why the gesture
			// unlock check below can safely inspect Howler.ctx right after this.
			Howler.volume(this.musicVolume);
		} catch {
			// Howler/AudioContext unavailable — audio degrades to silent no-op.
		}

		this.#unlockOnGesture();

		$effect.root(() => {
			$effect(() => {
				const trackId = resolveMusicForContext(storeNavigation.current);
				try {
					if (trackId) {
						this.#music.playTrack(trackId);
					} else {
						this.#music.stopAll();
					}
				} catch {
					// Playback backend unavailable — screen switches silently.
				}
			});
		});
	}

	/**
	 * @brief Browsers block audio playback until a user gesture. If Howler's
	 * AudioContext already exists but is suspended, attach one-shot
	 * click/keydown listeners that retry `ctx.resume()` and remove
	 * themselves once done — mirrors the fallback that used to live directly
	 * on an `<audio>` element in App.svelte, adapted to Howler's context.
	 */
	#unlockOnGesture(): void {
		try {
			const ctx = Howler.ctx;
			if (!ctx || ctx.state !== "suspended") return;

			const resume = () => {
				document.removeEventListener("click", resume);
				document.removeEventListener("keydown", resume);
				ctx.resume().catch(() => {
					// Still no gesture-granted permission — nothing further to do.
				});
			};
			document.addEventListener("click", resume);
			document.addEventListener("keydown", resume);
		} catch {
			// Howler.ctx unavailable — nothing to unlock.
		}
	}

	/**
	 * @brief Plays a one-shot SFX by catalog id. `opts.volume` falls back to
	 * this store's `sfxVolume` when not explicitly overridden per-call.
	 */
	playSfx(id: string, opts?: { pitch?: number; volume?: number }): void {
		try {
			this.#sfx.playSfx(id, { pitch: opts?.pitch, volume: opts?.volume ?? this.sfxVolume });
		} catch {
			// Underlying audio backend unavailable — silently drop the SFX.
		}
	}

	setMusicVolume(v: number): void {
		this.musicVolume = v;
		try {
			Howler.volume(v);
		} catch {
			// Howler/AudioContext unavailable — the setting still persists below.
		}
		this.#persist();
	}

	setSfxVolume(v: number): void {
		this.sfxVolume = v;
		this.#persist();
	}

	#persist(): void {
		try {
			localStorage.setItem(
				SETTINGS_STORAGE_KEY,
				JSON.stringify({ musicVolume: this.musicVolume, sfxVolume: this.sfxVolume })
			);
		} catch {
			// localStorage unavailable — settings still live in memory.
		}
	}
}

export const storeAudio = new StoreAudio();
