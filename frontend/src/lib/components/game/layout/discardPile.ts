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

import type { Card, CardType } from "$stores/game.svelte";

export interface DiscardEntry {
	card: Card;
	/** The color a wild was turned into, once one has been chosen for it. A wild
	 *  lands colorless and is told what it became a moment later — the server
	 *  only asks for the choice once the card is already down — and it has to
	 *  keep that color after the next card covers it, so it is recorded per
	 *  entry rather than read live off the state's single `active_type`. */
	wildColor?: CardType;
	/** Monotonic per-pile sequence — the render key. Card ids recycle when the
	 *  discard is reshuffled into the draw pile, so a replayed id can still sit
	 *  in the capped history; keying by id would then reuse the old low-in-the-
	 *  stack render block and the new card would appear UNDER the pile. */
	seq: number;
	/** Fixed rotation for this card on the pile, degrees. baseBeats.ts tweens a
	 *  landing card's own flight spin to this exact value (see
	 *  previewDiscardLanding below), so the pile's scatter is something the
	 *  card visibly rotates INTO rather than something it teleports onto. */
	rotationDeg: number;
	/** Fixed [x, y] offset from the pile center, em. */
	jitter: [number, number];
}

export const DISCARD_CAP = 30;

// The scatter jitter ONLY — a seat-relative base rotation (see discardEntryFor's
// baseRotationDeg) is layered on top of this, not folded into it, so cutting
// this from its old 30 keeps the pile from reading as messier than the base
// rotation alone already makes it.
export const MAX_ROTATION_DEG = 10;
export const MAX_JITTER_EM = 1.1;
/** CardMesh3D's own layered planes sit up to 0.004 world units apart; a
 *  per-card step smaller than that lets one card's layers interleave with its
 *  neighbor's (z-fighting) — 0.02 clears that with margin. Shared with
 *  DiscardPile3D (which stacks entries at this step) and baseBeats.ts (which
 *  has to know a landing card's exact future height to avoid a visible pop
 *  when it hands off from its flight to this static pile). */
export const DISCARD_STACK_STEP = 0.025;
export const BURIED_DEPTH_CAP = 20;
export const DISCARD_BURIED_TIER_DEPTH = 20;

/**
 * Geometric stack depth (Y in world space / Z in depth buffer) for an item in
 * the discard pile. Cards buried deep beyond the BURIED_DEPTH_CAP (20) share
 * a single bottom tier (height 0) so the pile's depth does not grow
 * unboundedly past 20 items.
 */
export function discardStackZ(index: number, totalCount?: number): number {
	if (totalCount === undefined) {
		return Math.min(index, BURIED_DEPTH_CAP) * DISCARD_STACK_STEP;
	}
	if (totalCount <= BURIED_DEPTH_CAP) {
		return index * DISCARD_STACK_STEP;
	}
	const buriedCount = totalCount - BURIED_DEPTH_CAP;
	if (index < buriedCount) {
		return 0;
	}
	return (index - buriedCount + 1) * DISCARD_STACK_STEP;
}

export const discardStackHeight = discardStackZ;

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
 *  the pile, instead of always retaking its one fixed spot.
 *
 *  `baseRotationDeg` is the seat-relative orientation the card was actually
 *  thrown from (0 for the local player, the opponent's seat facing +180 for
 *  everyone else — see baseBeats.svelte.ts) — the random jitter term is
 *  layered on TOP of it, not instead of it, so a card visibly keeps "which
 *  way it was thrown from" under the scatter instead of the scatter alone
 *  deciding its angle. */
export function discardEntryFor(card: Card, seq: number = 0, baseRotationDeg: number = 0): DiscardEntry {
	const seed = card.id + seq * 7919;
	const r1 = seeded(seed);
	const r2 = seeded(seed + 101);
	const r3 = seeded(seed + 211);
	return {
		card,
		seq,
		rotationDeg: baseRotationDeg + (r1 * 2 - 1) * MAX_ROTATION_DEG,
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
	cap: number = DISCARD_CAP,
	baseRotationDeg: number = 0
): DiscardEntry[] {
	const last = history[history.length - 1];
	if (last && last.card.id === card.id) return history;

	const next = [...history, discardEntryFor(card, (last?.seq ?? 0) + 1, baseRotationDeg)];
	return next.length > cap ? next.slice(next.length - cap) : next;
}

/**
 * Previews exactly what `appendDiscard` will produce for `card` — same entry
 * (deterministic from id+seq), plus the array index it will land at — without
 * mutating or actually appending. baseBeats.ts uses this to fly the card
 * straight to the precise spot (jitter offset AND stack height) it's about to
 * statically render at, so the handoff from flight to pile is a no-op instead
 * of a visible pop to a different position.
 */
export function previewDiscardLanding(
	history: DiscardEntry[],
	card: Card,
	cap: number = DISCARD_CAP,
	baseRotationDeg: number = 0
): { entry: DiscardEntry; index: number; z: number } {
	const last = history[history.length - 1];
	if (last && last.card.id === card.id) {
		const index = history.length - 1;
		return { entry: last, index, z: discardStackZ(index, history.length) };
	}
	const entry = discardEntryFor(card, (last?.seq ?? 0) + 1, baseRotationDeg);
	const currentTopZ =
		history.length > 0 ? discardStackZ(history.length - 1, history.length) : 0;
	const nextLength = Math.min(history.length + 1, cap);
	const index = nextLength - 1;
	const nominalZ = discardStackZ(index, nextLength);
	const z = Math.max(nominalZ, currentTopZ + DISCARD_STACK_STEP);
	return { entry, index, z };
}

const WILD_VALUES = new Set(["jolly", "jolly_draw4"]);

export function isWildCard(card: Card): boolean {
	return WILD_VALUES.has(card.value);
}

/**
 * Records the table's active color on the pile's top card, if that card is a
 * wild waiting to find out what it turned into. `"white"` is the absence of a
 * choice, not a color, so it never paints anything. Never mutates the input —
 * returns the same array when there is nothing to paint.
 */
export function paintTopWild(history: DiscardEntry[], color: CardType): DiscardEntry[] {
	const top = history[history.length - 1];
	if (!top || color === "white") return history;
	if (!isWildCard(top.card) || top.wildColor === color) return history;

	const next = [...history];
	next[next.length - 1] = { ...top, wildColor: color };
	return next;
}
