/**
 * @file topbarContent.svelte.ts
 * @brief Lets the currently-rendered screen hand TopBar a snippet to render
 * in its middle content slot (baseline TopBar layout: title | content | avatar).
 * A screen sets this in an $effect on mount and clears it (returns undefined)
 * on unmount, so switching screens never leaves a stale slot behind.
 */
import type { Snippet } from "svelte";

class StoreTopbarContent {
	current = $state<Snippet | undefined>(undefined);
}

export const storeTopbarContent = new StoreTopbarContent();
