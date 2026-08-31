/**
 * @file session.ts
 * @brief Wires session-scoped stores to the end of a session.
 *
 * The stores are module singletons, so their contents outlive the session
 * that populated them unless something clears them. This module is the one
 * place that knows the full set.
 */

import { storeAuth } from "$stores/auth.svelte";
import { chatStore } from "$stores/chat.svelte";
import { storeLobby } from "$stores/lobby.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeStats } from "$stores/stats.svelte";
import type { SessionStore } from "$stores/sessionStore";

const sessionStores: SessionStore[] = [chatStore, storeLobby, storeGame, storeStats];

let installed = false;

/**
 * @brief Subscribes the session stores to the auth store's logout signal.
 * Idempotent: calling it more than once registers a single subscription.
 */
export function installSessionResets(): void {
	if (installed) return;
	installed = true;

	storeAuth.onLoggedOut(() => {
		for (const store of sessionStores) store.reset();
	});
}
