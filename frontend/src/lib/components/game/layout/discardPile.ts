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

/** Safety valve on the client-side history length. The stack HEIGHT is bounded
 *  by compression (see discardStackZ), so this is an allocation guard only, not
 *  a visual device: it is set high enough that an ordinary match reaches the
 *  reshuffle (which trims the pile) long before it. Deliberately NOT derived
 *  from a deck size — mods can change that per match, so nothing here may
 *  assume a particular deck size. */
export const DISCARD_CAP = 128;

// The scatter jitter ONLY — a seat-relative base rotation (see discardEntryFor's
// baseRotationDeg) is layered on top of this, not folded into it, so cutting
// this from its old 30 keeps the pile from reading as messier than the base
// rotation alone already makes it.
export const MAX_ROTATION_DEG = 10;
export const MAX_JITTER_EM = 1.1;
/** Comfortable per-card stack step; used until the pile would grow past
 *  MAX_DISCARD_HEIGHT, then compressed. Shared with DiscardPile3D (which stacks
 *  entries by it) and baseBeats.ts (which must know a landing card's exact
 *  future depth to avoid a visible pop when a flight hands off to this pile). */
export const DISCARD_STACK_STEP = 0.025;
/** Visual height budget for the whole pile, world units. Every card keeps its
 *  OWN depth inside this budget — the step compresses as the pile grows instead
 *  of cards being dropped, collapsed onto a shared tier, or the pile towering.
 *  Past MAX_DISCARD_HEIGHT / MIN_DISCARD_STEP (150 cards) the pile must grow
 *  again; that is deliberately preferred over dropping or collapsing cards,
 *  and every card still keeps a distinct depth. */
export const MAX_DISCARD_HEIGHT = 0.6;
/** Precision floor. CardMesh3D's own planes span up to 0.004 world units and
 *  the depth buffer resolves far finer than that, so a step of 0.004 still
 *  keeps neighbouring cards distinct. */
export const MIN_DISCARD_STEP = 0.004;

/**
 * The stack step for a pile of `count` cards: the comfortable step until the
 * pile would exceed MAX_DISCARD_HEIGHT, then compressed, floored at
 * MIN_DISCARD_STEP. Depends only on the CURRENT pile length, never on a deck
 * size, so modded decks of any size are handled without special cases.
 */
export function discardStepFor(count: number): number {
	const n = Math.max(1, count);
	return Math.min(DISCARD_STACK_STEP, Math.max(MIN_DISCARD_STEP, MAX_DISCARD_HEIGHT / n));
}

/**
 * Geometric stack depth (Y in world space / Z in depth buffer) for an item in
 * the discard pile, by its position `index` from the bottom.
 *
 * Every card gets its OWN depth, compressed into MAX_DISCARD_HEIGHT. An earlier
 * version collapsed cards deeper than a 20-card tier onto a shared bottom tier
 * (all at y = 0), which put many card planes — and their transparent shadow
 * planes — at exactly the same depth; that renders as dense Z-fighting stripes
 * (verified by forcing the whole stack to one Y: the stripes appear at once).
 * Dropping old cards instead would make the pile visibly stop growing and sink,
 * so the pile is compressed rather than truncated.
 */
export function discardStackZ(index: number, totalCount?: number): number {
	return index * discardStepFor(totalCount ?? index + 1);
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
export function discardEntryFor(
	card: Card,
	seq: number = 0,
	baseRotationDeg: number = 0
): DiscardEntry {
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
	const currentTopZ = history.length > 0 ? discardStackZ(history.length - 1, history.length) : 0;
	const nextLength = Math.min(history.length + 1, cap);
	const index = nextLength - 1;
	const nominalZ = discardStackZ(index, nextLength);
	// Must use the same (compressed) step the pile will actually render at, not
	// the comfortable baseline, or the guard overshoots and the card pops.
	const z = Math.max(nominalZ, currentTopZ + discardStepFor(nextLength));
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
