/**
 * @file turnOrder.ts
 * @brief Pure "prev-2 / current / next-2" turn-order window used by
 * TurnOrderStrip.svelte. No Svelte, no store access — takes a plain player
 * list + current turn + play direction.
 */

import type { GamePlayer } from "$stores/game.svelte";

export interface TurnOrderWindow {
	/** Always exactly `radius` long. Nearest-first-from-current. `null` only
	 *  when there are 0-1 other players to show (nothing to repeat). */
	prev: (GamePlayer | null)[];
	current: GamePlayer | null;
	/** Always exactly `radius` long. Nearest-first-from-current. */
	next: (GamePlayer | null)[];
}

export function computeTurnOrderWindow(
	players: GamePlayer[],
	currentTurn: string,
	playDirection: number,
	radius = 2
): TurnOrderWindow {
	const n = players.length;
	const idx = players.findIndex((p) => p.username === currentTurn);
	if (idx === -1 || n === 0) {
		return {
			prev: new Array(radius).fill(null),
			current: null,
			next: new Array(radius).fill(null)
		};
	}

	const dir = playDirection >= 0 ? 1 : -1;
	// n - 1 other players to draw from; below that there's nothing left to
	// repeat and the slot pads with null instead (e.g. a 1-player table).
	const others = n - 1;

	const prev: (GamePlayer | null)[] = [];
	const next: (GamePlayer | null)[] = [];
	for (let i = 1; i <= radius; i++) {
		if (others <= 0) {
			prev.push(null);
			next.push(null);
			continue;
		}
		next.push(players[(((idx + dir * i) % n) + n) % n]);
		prev.push(players[(((idx - dir * i) % n) + n) % n]);
	}

	return { prev, current: players[idx], next };
}
