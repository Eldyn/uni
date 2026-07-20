/**
 * @file turnOrder.ts
 * @brief Pure "prev-2 / current / next-2" turn-order window used by
 * TurnOrderStrip.svelte. No Svelte, no store access — takes a plain player
 * list + current turn + play direction.
 */

import type { GamePlayer } from "$stores/game.svelte";

export interface TurnOrderWindow {
	/** Players before the current turn, ordered nearest-first-from-current. */
	prev: GamePlayer[];
	current: GamePlayer | null;
	/** Players after the current turn, ordered nearest-first-from-current. */
	next: GamePlayer[];
}

/**
 * Walks the player list in `playDirection` starting from whoever currently
 * has the turn, returning up to `radius` players on each side. Caps the
 * radius so a small table never shows the same player on both sides.
 */
export function computeTurnOrderWindow(
	players: GamePlayer[],
	currentTurn: string,
	playDirection: number,
	radius = 2
): TurnOrderWindow {
	const n = players.length;
	const idx = players.findIndex((p) => p.username === currentTurn);
	if (idx === -1 || n === 0) return { prev: [], current: null, next: [] };

	const dir = playDirection >= 0 ? 1 : -1;
	const maxRadius = Math.min(radius, Math.floor((n - 1) / 2));

	const prev: GamePlayer[] = [];
	const next: GamePlayer[] = [];
	for (let i = 1; i <= maxRadius; i++) {
		next.push(players[(((idx + dir * i) % n) + n) % n]);
		prev.push(players[(((idx - dir * i) % n) + n) % n]);
	}

	return { prev, current: players[idx], next };
}
