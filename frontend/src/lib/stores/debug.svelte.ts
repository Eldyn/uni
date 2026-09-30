/**
 * @file debug.svelte.ts
 * @brief Tester-facing debug flag. Enabled by `?debug` in the URL or by the
 * persisted `uni:debug` key; there is deliberately no player-facing toggle.
 */
export const DEBUG_STORAGE_KEY = "uni:debug";

export class DebugFlag {
	#enabled = $state(false);

	constructor(search: string = typeof location === "undefined" ? "" : location.search) {
		this.#enabled = new URLSearchParams(search).has("debug") || readStoredFlag();
	}

	get enabled(): boolean {
		return this.#enabled;
	}

	set enabled(value: boolean) {
		this.#enabled = value;
		try {
			if (value) localStorage.setItem(DEBUG_STORAGE_KEY, "1");
			else localStorage.removeItem(DEBUG_STORAGE_KEY);
		} catch {
			// localStorage unavailable
		}
	}
}

function readStoredFlag(): boolean {
	try {
		return typeof localStorage !== "undefined" && localStorage.getItem(DEBUG_STORAGE_KEY) === "1";
	} catch {
		return false;
	}
}

export const storeDebug = new DebugFlag();
