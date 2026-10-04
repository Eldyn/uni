/**
 * @file audio.svelte.ts
 * @brief Reactive store owning music/SFX volume settings, the shared
 * MusicPlayer/SfxPlayer instances, and screen-driven music switching.
 */

import { Howler } from "howler";
import { SFX_CATALOG } from "$data/audioCatalogs";
import { MusicPlayer } from "$lib/audio/musicPlayer";
import { SfxPlayer } from "$lib/audio/sfxPlayer";
import { isMobileDevice, resolveMusicForContext } from "$lib/audio/audioLogic";
import { storeNavigation } from "$stores/navigation.svelte";

const SETTINGS_STORAGE_KEY = "uni:audio:settings";
const DEFAULT_MUSIC_VOLUME = 0.15;
const DEFAULT_SFX_VOLUME = 0.8;

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
			// INFO: localStorage unavailable or malformed, fall back to defaults.
		}
	}

	/**
	 * @brief Boots the audio system: applies the persisted music volume,
	 * wires the autoplay-unlock-on-gesture fallback, and starts/keeps
	 * screen-driven music in sync with `storeNavigation`. Idempotent, safe
	 * to call more than once. Degrades to a silent no-op if the underlying
	 * Howler/AudioContext is unavailable in this environment.
	 */
	init(): void {
		if (this.#initialized) return;
		this.#initialized = true;

		try {
			this.#music.setVolume(this.musicVolume);
		} catch {
			// INFO: Howler/AudioContext unavailable, degrade to silent no-op.
		}

		this.#unlockOnGesture();
		this.#bindVisibilityPause();

		// INFO: storeAudio is an app-lifetime singleton, so this effect is
		//       meant to run for the whole session, the dispose function
		//       $effect.root returns is intentionally left unused.
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
					// INFO: Playback backend unavailable, screen switches silently.
				}
			});
		});
	}

	/**
	 * @brief Browsers block audio playback until a user gesture. If Howler's
	 * AudioContext exists but is suspended, attach one-shot click/keydown
	 * listeners that retry `ctx.resume()` and remove themselves once done.
	 */
	#unlockOnGesture(): void {
		try {
			const ctx = Howler.ctx;
			if (!ctx || ctx.state !== "suspended") return;

			const resume = () => {
				document.removeEventListener("click", resume);
				document.removeEventListener("keydown", resume);
				ctx.resume().catch(() => {
					// INFO: Still no gesture-granted permission, nothing to do.
				});
			};
			document.addEventListener("click", resume);
			document.addEventListener("keydown", resume);
		} catch {
			// INFO: Howler.ctx unavailable, nothing to unlock.
		}
	}

	/**
	 * @brief Backgrounding a mobile page (tab switch, app switch, screen lock)
	 * does not unload it, so without this the shared AudioContext keeps
	 * rendering and audio plays on. Suspend it while hidden, resume on return.
	 * Mobile-only: desktop browsers keep the music going in a hidden tab by
	 * design. Both Howler tracks and the raw Web Audio multi-channel stems
	 * route through `Howler.ctx`, so one suspend covers the whole audio system.
	 */
	#bindVisibilityPause(): void {
		document.addEventListener("visibilitychange", this.#onVisibilityChange);
	}

	#onVisibilityChange = (): void => {
		try {
			if (!this.#shouldPauseOnHidden()) return;
			const ctx = Howler.ctx;
			if (!ctx) return;
			if (document.hidden) {
				if (ctx.state === "running") {
					ctx.suspend().catch(() => {
						// INFO: Already suspended or unsupported, nothing to do.
					});
				}
			} else if (ctx.state === "suspended") {
				ctx.resume().catch(() => {
					// INFO: No gesture-granted permission yet, retry on gesture.
				});
			}
		} catch {
			// INFO: Howler.ctx unavailable, nothing to pause/resume.
		}
	};

	#shouldPauseOnHidden(): boolean {
		try {
			return isMobileDevice({
				userAgent: navigator.userAgent ?? "",
				touchPrimary:
					typeof window.matchMedia === "function" &&
					window.matchMedia("(pointer: coarse) and (hover: none)").matches
			});
		} catch {
			return false;
		}
	}

	/**
	 * @brief Plays a one-shot SFX by catalog id. `opts.volume` falls back to
	 * this store's `sfxVolume` scaled by the catalog entry's own `volume`
	 * when not explicitly overridden per-call. `opts.gain` scales whichever
	 * volume applies, e.g. to play an opponent's action quieter.
	 */
	playSfx(id: string, opts?: { pitch?: number; volume?: number; gain?: number }): void {
		try {
			const catalogGain = SFX_CATALOG[id]?.volume ?? 1;
			this.#sfx.playSfx(id, {
				pitch: opts?.pitch,
				volume: (opts?.volume ?? this.sfxVolume * catalogGain) * (opts?.gain ?? 1)
			});
		} catch {
			// INFO: Audio backend unavailable, silently drop the SFX.
		}
	}

	setMusicVolume(v: number): void {
		this.musicVolume = v;
		try {
			this.#music.setVolume(v);
		} catch {
			// INFO: Playback backend unavailable, the setting still persists.
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
			// INFO: localStorage unavailable, settings still live in memory.
		}
	}
}

export const storeAudio = new StoreAudio();
