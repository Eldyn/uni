/**
 * @file sessionStore.ts
 * @brief Contract for stores whose contents belong to one authenticated
 * session. These are module singletons, so without an explicit reset their
 * state outlives the session that populated it and leaks into the next login.
 */

/**
 * A store holding session-scoped state.
 */
export interface SessionStore {
	/**
	 * Restores the store's session-scoped state (the fields particular to the
	 * logged-in session) to its constructed defaults.
	 *
	 * This interface makes no guarantee about WebSocket handler registration
	 * lifecycle one way or the other — that's each implementation's own
	 * concern, and is documented on that implementation's `reset()` (and,
	 * where relevant, on its own separate teardown method, e.g.
	 * `chatStore.dispose()`) rather than here.
	 */
	reset(): void;
}
