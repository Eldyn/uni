/**
 * @file spectator.svelte.ts
 * @brief Owns which player's POV a spectator is currently viewing. Kept
 * separate from storeGame so both the topbar avatar-row click and
 * the in-scene opponent click can drive the same piece of state
 * without one UI surface importing the other.
 */
import { storeGame } from "./game.svelte";

class StoreSpectator {
	/** Username of the player whose POV is currently being viewed, or null if
	 *  no explicit choice has been made yet (defaults to the local view). */
	viewedUsername = $state<string | null>(null);

	/** True while the local client is spectating (mirrors storeGame.isSpectator,
	 *  exposed here too so consumers of this store don't also need storeGame). */
	isSpectating = $derived(storeGame.isSpectator);

	setViewedUsername(username: string): void {
		if (!this.isSpectating) return;
		this.viewedUsername = username;
	}

	reset(): void {
		this.viewedUsername = null;
	}
}

export const storeSpectator = new StoreSpectator();
