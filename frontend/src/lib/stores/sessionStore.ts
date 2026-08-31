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
	 * Returns the store to its constructed state.
	 *
	 * Clears session data only. WebSocket handler registrations are NOT
	 * removed: they are registered once for the lifetime of the module and
	 * must survive a session change, or the next session receives nothing.
	 */
	reset(): void;
}
