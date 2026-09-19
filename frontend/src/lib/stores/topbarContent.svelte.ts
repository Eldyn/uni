/**
 * @file topbarContent.svelte.ts
 * @brief Lets the currently-rendered screen hand TopBar snippets to render in
 * its chrome: `current` fills the middle content slot (title | content |
 * avatar), `actions` fills the right-side action cluster on mobile only, for
 * controls that get promoted out of a screen's own secondary header on small
 * screens. A screen sets these in an $effect on mount and clears them (returns
 * undefined) on unmount, so switching screens never leaves a stale slot behind.
 */
import type { Snippet } from "svelte";

class StoreTopbarContent {
	current = $state<Snippet | undefined>(undefined);
	actions = $state<Snippet | undefined>(undefined);
}

export const storeTopbarContent = new StoreTopbarContent();
