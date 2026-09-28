/**
 * @file gameLoader.svelte.ts
 * @brief Publishing surface for the game loading screen's visibility.
 *
 * `GameLoader` owns the real `shown` derivation (waiting for players, or the
 * minimum card cycle still running); it mirrors that value here so the board —
 * which is mounted behind the loader and would otherwise start the match-start
 * deal cinematic where nobody can see it — can defer the intro until the
 * loader has unmounted.
 */

class StoreGameLoader {
	/** True while the loading screen still covers the board. */
	shown = $state(false);
}

export const storeGameLoader = new StoreGameLoader();
