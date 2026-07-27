/**
 * @file devFixturePreset.svelte.ts
 * @brief Card ids the harness pre-arms as selected/hovered, so the touch halo
 * and mouse hover-lift can be screenshotted without a live gesture driving them.
 */

class DevFixturePreset {
	selectId = $state<number | null>(null);
	hoverId = $state<number | null>(null);
}

export const devFixturePreset = new DevFixturePreset();
