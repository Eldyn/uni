/**
 * @file deckCatalog.svelte.ts
 * @brief Reactive store for the deck catalog served by `GET /api/decks`.
 *
 * The catalog feeds the lobby deck picker. It is public content (deck files,
 * not user data), so the request carries no auth token beyond the session
 * cookie, matching the leaderboard endpoint.
 */

/**
 * @interface DeckInfo
 * @brief A single deck offered by the catalog.
 */
export interface DeckInfo {
	/** Deck id (`namespace:id` when namespaced, bare id otherwise). */
	id: string;
	/** Human-readable deck name shown in the picker. */
	name: string;
	/** Id of the mod that owns the deck. */
	namespace: string;
	/** Ids of the mods this deck depends on and activates. */
	mods: string[];
}

/**
 * @class StoreDeckCatalog
 * @brief Encapsulates the state and retrieval of the deck catalog.
 */
class StoreDeckCatalog {
	/** Reactive state containing every deck the server offers. */
	decks = $state<DeckInfo[]>([]);

	/** Boolean flag indicating whether a catalog request is in progress. */
	isLoading = $state(false);

	/** Boolean flag set when the last request failed. */
	error = $state(false);

	/**
	 * @brief Retrieves the deck catalog from the server.
	 * Makes a GET request to `/api/decks` including the session cookie. On
	 * success, replaces `decks`; on failure, sets `error` and leaves the
	 * previous list untouched so the picker keeps working.
	 * Idempotent: a loaded (non-empty) catalog is only refetched when `force`
	 * is set, and concurrent calls share the in-flight request.
	 * @param force Refetch even when a catalog is already loaded.
	 * @returns Empty promise resolved when the network operation completes.
	 */
	async fetchDecks(force = false): Promise<void> {
		if (this.isLoading) return;
		if (!force && this.decks.length > 0) return;

		this.isLoading = true;
		this.error = false;

		try {
			const res = await fetch("/api/decks", {
				credentials: "include",
				headers: { "Content-Type": "application/json" }
			});

			if (res.ok) {
				const data = await res.json();
				this.decks = data.decks ?? [];
			} else {
				this.error = true;
			}
		} catch (e) {
			console.error("[deckCatalog] Failed to load deck catalog:", e);
			this.error = true;
		} finally {
			this.isLoading = false;
		}
	}
}

/**
 * @brief Singleton instance exported for use by the Svelte components.
 */
export const storeDeckCatalog = new StoreDeckCatalog();
