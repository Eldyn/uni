/**
 * @file discardPile.ts
 * @brief Pure client-side discard-history model. The server only ever sends a
 * single `top_card`, never a pile, so a real "stack of dropped cards" has to be
 * accumulated here: every time a new top card is revealed it is appended with a
 * random-but-stable rotation + jitter (seeded once from the card id, so a
 * re-render never reflows the scatter). The list is capped so the pile never
 * grows without bound; the oldest entries evict first (the renderer sweeps them
 * under the draw pile). No Svelte, no DOM — just the numbers the renderer draws.
 */

import type { Card } from "$stores/game.svelte";

export interface DiscardEntry {
	card: Card;
	/** Monotonic per-pile sequence — the render key. Card ids recycle when the
	 *  discard is reshuffled into the draw pile, so a replayed id can still sit
	 *  in the capped history; keying by id would then reuse the old low-in-the-
	 *  stack render block and the new card would appear UNDER the pile. */
	seq: number;
	/** Fixed rotation for this card on the pile, degrees. */
	rotationDeg: number;
	/** Fixed [x, y] offset from the pile center, em. */
	jitter: [number, number];
}

export const DISCARD_CAP = 30;
export const MAX_ROTATION_DEG = 30;
const MAX_JITTER_EM = 1.1;

/**
 * Deterministic pseudo-random in [0, 1) from an integer seed. Same seed always
 * yields the same value, so a card's scatter is fixed by its id and stable
 * across every re-render.
 */
function seeded(seed: number): number {
	const x = Math.sin(seed * 12.9898) * 43758.5453;
	return x - Math.floor(x);
}

/** The fixed scatter (rotation + jitter) for a card. Seeded off id AND seq so
 *  the same physical card lands differently each time it cycles back through
 *  the pile, instead of always retaking its one fixed spot. */
export function discardEntryFor(card: Card, seq: number = 0): DiscardEntry {
	const seed = card.id + seq * 7919;
	const r1 = seeded(seed);
	const r2 = seeded(seed + 101);
	const r3 = seeded(seed + 211);
	return {
		card,
		seq,
		rotationDeg: (r1 * 2 - 1) * MAX_ROTATION_DEG,
		jitter: [(r2 * 2 - 1) * MAX_JITTER_EM, (r3 * 2 - 1) * MAX_JITTER_EM]
	};
}

/**
 * Append the newly-revealed top card to the pile, keeping at most `cap`
 * entries with the oldest evicted first. Re-revealing the current top card
 * (same id) is a no-op so a re-render never stacks a duplicate. Never mutates
 * the input — returns a fresh array.
 */
export function appendDiscard(
	history: DiscardEntry[],
	card: Card,
	cap: number = DISCARD_CAP
): DiscardEntry[] {
	const last = history[history.length - 1];
	if (last && last.card.id === card.id) return history;

	const next = [...history, discardEntryFor(card, (last?.seq ?? 0) + 1)];
	return next.length > cap ? next.slice(next.length - cap) : next;
}
