/**
 * @file orientation.svelte.ts
 * @brief Reactive store owning the "switch to landscape in game" preference
 * and driving the Screen Orientation API while a match is open.
 *
 * Auto-rotating the screen is only possible on Android/Chromium browsers:
 * `ScreenOrientation.lock()` is unimplemented in WebKit (so iPhone/iPad and
 * every installed iOS PWA are out), and desktop Chrome's `lock()` always
 * throws NotSupportedError. The setting is only surfaced where the capability
 * actually exists; everywhere else the store is inert and the board's existing
 * portrait/landscape layout takes over.
 */

import { isMobileDevice } from "$lib/audio/audioLogic";
import { storeNavigation } from "./navigation.svelte";
import { storeGame } from "./game.svelte";

const SETTINGS_STORAGE_KEY = "uni:orientation:settings";

interface OrientationSettings {
	switchToLandscape: boolean;
}

export interface OrientationLockHints {
	userAgent: string;
	touchPrimary: boolean;
	hasLockFunction: boolean;
}

/**
 * @brief Whether this device can auto-rotate the screen. Requires both a
 * mobile device and a callable `screen.orientation.lock` — the latter alone is
 * not enough because desktop Chrome exposes the method but always rejects.
 * Pure so it can be unit-tested without a DOM.
 */
export function supportsOrientationLock(hints: OrientationLockHints): boolean {
	if (!hints.hasLockFunction) return false;
	return isMobileDevice({ userAgent: hints.userAgent, touchPrimary: hints.touchPrimary });
}

function readLockHints(): OrientationLockHints {
	if (typeof window === "undefined") {
		return { userAgent: "", touchPrimary: false, hasLockFunction: false };
	}
	const orientation = (screen as Screen | undefined)?.orientation;
	return {
		userAgent: navigator.userAgent ?? "",
		touchPrimary:
			typeof window.matchMedia === "function" &&
			window.matchMedia("(pointer: coarse) and (hover: none)").matches,
		hasLockFunction: typeof orientation?.lock === "function"
	};
}

class StoreOrientation {
	/** Default on: phones that can rotate get the wider board automatically. */
	switchToLandscape = $state<boolean>(true);

	#initialized = false;

	constructor() {
		try {
			const raw =
				typeof localStorage !== "undefined" ? localStorage.getItem(SETTINGS_STORAGE_KEY) : null;
			const parsed = raw ? (JSON.parse(raw) as Partial<OrientationSettings>) : null;
			if (typeof parsed?.switchToLandscape === "boolean") {
				this.switchToLandscape = parsed.switchToLandscape;
			}
		} catch {
			// INFO: localStorage unavailable or malformed, fall back to defaults.
		}
	}

	/** True only where a runtime orientation lock can actually take effect. */
	get supported(): boolean {
		return supportsOrientationLock(readLockHints());
	}

	setSwitchToLandscape(value: boolean): void {
		this.switchToLandscape = value;
		if (!value) this.unlock();
		this.#persist();
	}

	/**
	 * @brief Boots the in-game orientation watcher. Locks to landscape while
	 * the game screen owns a live match and the setting is enabled, and
	 * releases the lock on every other screen / when disabled. Assumes the
	 * capability check already passed; lock refusals are swallowed so the game
	 * keeps working on browsers that impose a fullscreen pre-condition.
	 */
	init(): void {
		if (this.#initialized) return;
		this.#initialized = true;

		// INFO: storeOrientation is an app-lifetime singleton; the dispose
		//       function $effect.root returns is intentionally left unused,
		//       matching storeAudio.
		$effect.root(() => {
			$effect(() => {
				const inGame = storeNavigation.current === "game" && storeGame.state !== null;
				if (inGame && this.switchToLandscape && this.supported) {
					this.#lockLandscape();
				} else {
					this.unlock();
				}
			});
		});
	}

	#lockLandscape(): void {
		try {
			const orientation = (screen as Screen | undefined)?.orientation;
			if (typeof orientation?.lock !== "function") return;
			orientation.lock("landscape").catch(() => {
				// INFO: Browser refused (e.g. needs fullscreen, or a lock is
				//       already pending); non-fatal, the board adapts instead.
			});
		} catch {
			// INFO: Screen Orientation API unavailable, nothing to lock.
		}
	}

	unlock(): void {
		try {
			const orientation = (screen as Screen | undefined)?.orientation;
			if (typeof orientation?.unlock !== "function") return;
			orientation.unlock();
		} catch {
			// INFO: Screen Orientation API unavailable, nothing to unlock.
		}
	}

	#persist(): void {
		try {
			if (typeof localStorage === "undefined") return;
			const data: OrientationSettings = { switchToLandscape: this.switchToLandscape };
			localStorage.setItem(SETTINGS_STORAGE_KEY, JSON.stringify(data));
		} catch {
			// INFO: localStorage unavailable, setting stays in-memory only.
		}
	}
}

export const storeOrientation = new StoreOrientation();
