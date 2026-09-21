/**
 * @file promptTargets.ts
 * @brief Target eligibility for the active `choose_player` prompt, shared by
 * the renderer, the board's seat highlighting and the animation dimming logic
 * so all three can never disagree about who is selectable.
 */

import { storeGame } from "$stores/game.svelte";

/**
 * @brief Usernames the active `choose_player` prompt offers as targets.
 *
 * Uses `payload.options` when the server supplies an array of usernames;
 * otherwise every player except the local one is a valid target.
 */
export function choosePlayerTargets(): string[] {
	const prompt = storeGame.activePrompt;
	if (prompt?.kind !== "choose_player") return [];

	const options = (prompt.payload as Record<string, unknown> | null | undefined)?.options;
	if (Array.isArray(options) && options.every((option) => typeof option === "string")) {
		return options as string[];
	}

	return (storeGame.state?.players ?? [])
		.filter((player) => player.username !== storeGame.localPlayer?.username)
		.map((player) => player.username);
}

/**
 * @brief Whether `username` is an eligible target of the active
 * `choose_player` prompt.
 */
export function isChoosePlayerTarget(username: string): boolean {
	return choosePlayerTargets().includes(username);
}
