/**
 * @file avatar.svelte.ts
 * @brief Reactive store for the local account's equipped avatar loadout.
 * Talks to the loadout REST endpoint and keeps the last known state.
 */

import type { Loadout } from "$lib/avatar/types";
import { parseLoadout } from "$lib/avatar/loadoutSchema";
import type { SessionStore } from "$stores/sessionStore";

/** Lifecycle of the local loadout: nothing fetched, in flight, loaded, failed. */
export type AvatarStatus = "idle" | "loading" | "ready" | "error";

/** Result of a save: ok, or the server's rejection code. */
export type AvatarSaveResult = { ok: true } | { ok: false; code: string };

/** The loadout endpoint, relative to the app origin. */
const LOADOUT_URL = "/avatar/loadout/me";

/**
 * @class StoreAvatar
 * @brief Holds the local account's equipped loadout and syncs it with the server.
 */
class StoreAvatar implements SessionStore {
	/** The local account's equipped loadout, or null before the first fetch. */
	loadout = $state<Loadout | null>(null);

	/** Where the local loadout currently is in its request lifecycle. */
	status = $state<AvatarStatus>("idle");

	/**
	 * @brief Fetches the local account's loadout from the server.
	 * A missing or unsaved loadout resolves to the server's default look; a
	 * malformed payload leaves `loadout` empty rather than throwing.
	 */
	async fetchMine(): Promise<void> {
		this.status = "loading";
		try {
			const res = await fetch(LOADOUT_URL, {
				credentials: "include",
				headers: { "Content-Type": "application/json" }
			});

			if (!res.ok) {
				this.status = "error";
				return;
			}

			const data = await res.json();
			this.loadout = parseLoadout(data.loadout);
			this.status = "ready";
		} catch {
			this.status = "error";
		}
	}

	/**
	 * @brief Saves a whole loadout atomically.
	 * The server validates every id, slot and swatch and either accepts or
	 * rejects the whole payload; a rejection returns its error code.
	 * @param next The loadout to save.
	 */
	async save(next: Loadout): Promise<AvatarSaveResult> {
		try {
			const res = await fetch(LOADOUT_URL, {
				method: "PUT",
				credentials: "include",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify(next)
			});

			if (res.ok) {
				const data = await res.json();
				this.loadout = parseLoadout(data.loadout) ?? next;
				this.status = "ready";
				return { ok: true };
			}

			const body = await res.json().catch(() => null);
			const code =
				body !== null && typeof body === "object" && typeof body.error === "string"
					? body.error
					: "unknown";
			return { ok: false, code };
		} catch {
			return { ok: false, code: "network" };
		}
	}

	/** @brief Clears the previous account's loadout on logout. */
	reset(): void {
		this.loadout = null;
		this.status = "idle";
	}
}

/** Singleton instance exported for use by the Svelte components. */
export const storeAvatar = new StoreAvatar();
