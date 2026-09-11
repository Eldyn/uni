/**
 * @file handLine.ts
 * @brief Pure per-card geometry for the LOCAL player's own hand: a straight,
 * slightly overlapping row (not a fan/arc like handFan.ts, and not a ring
 * like handRing.ts — the local hand is the one hand the player can actually
 * read card values on and drag to reorder, so it reads best as a literal
 * line). No Svelte, no CSS, no DOM — every slot is a plain {x, rotateDeg}
 * offset from the row's own center, in em, consumed directly by the Threlte
 * renderer.
 *
 * The row does NOT compress without limit. Past a certain overlap a card is a
 * sliver with no readable value on it, which is unusable on a touch screen
 * where there's no hover to reveal it — so spacing bottoms out at
 * CARD_LINE_MIN_SPACING_EM and the row becomes a scrollable strip instead.
 */

export interface CardLineSlot {
	/** Offset from the row's center, in em, with the scroll offset applied. */
	x: number;
	rotateDeg: number;
}

export interface HandLineLayout {
	slots: CardLineSlot[];
	/** Center-to-center spacing actually used, in em. */
	spacingEm: number;
	/** Half the distance between the outermost card centers, in em. */
	halfSpanEm: number;
	/** How far the row may scroll from center in either direction, in em.
	 *  Zero while the whole row still fits. */
	maxScrollEm: number;
	/** The clamped scroll offset the returned slots were built with. */
	scrollEm: number;
}

/** Horizontal spacing between adjacent card centers, in em — smaller than the
 *  card's own ~5em width so neighbors slightly overlap. */
export const CARD_LINE_SPACING_EM = 4;

/** The tightest the row may pack. Cards are ~5em wide, so this still leaves
 *  60% of every card showing — enough to read its color and value without
 *  hovering it. */
export const CARD_LINE_MIN_SPACING_EM = 3;

/**
 * Lays out one slot per card along a straight horizontal row. Spacing shrinks
 * from CARD_LINE_SPACING_EM toward CARD_LINE_MIN_SPACING_EM as the row outgrows
 * `maxHalfSpanEm`, and stops there; whatever still doesn't fit turns into
 * scroll range, which `scrollEm` pans through (positive = the view moves right,
 * revealing cards off the left end). `scrollEm` is clamped for the caller.
 */
export function computeHandLine(
	cardCount: number,
	maxHalfSpanEm: number = Infinity,
	scrollEm: number = 0
): HandLineLayout {
	if (cardCount <= 0) {
		return {
			slots: [],
			spacingEm: CARD_LINE_SPACING_EM,
			halfSpanEm: 0,
			maxScrollEm: 0,
			scrollEm: 0
		};
	}

	const gaps = Math.max(1, cardCount - 1);
	const spacingEm = Math.max(
		CARD_LINE_MIN_SPACING_EM,
		Math.min(CARD_LINE_SPACING_EM, (2 * maxHalfSpanEm) / gaps)
	);

	const halfSpanEm = (spacingEm * (cardCount - 1)) / 2;
	// Once the row overflows, the scroll range spans the row's whole half-span
	// rather than just the overhang past maxHalfSpanEm — that overhang-only
	// range used to strand the outermost cards inside the edge-fade zone,
	// never reachable at full opacity, because it capped scrolling well short
	// of center. The full half-span lets the outermost card reach x=0.
	const maxScrollEm = halfSpanEm > maxHalfSpanEm ? halfSpanEm : 0;
	const clampedScroll = Math.max(-maxScrollEm, Math.min(maxScrollEm, scrollEm));

	const center = (cardCount - 1) / 2;
	return {
		slots: Array.from({ length: cardCount }, (_, i) => ({
			x: (i - center) * spacingEm - clampedScroll,
			rotateDeg: 0
		})),
		spacingEm,
		halfSpanEm,
		maxScrollEm,
		scrollEm: clampedScroll
	};
}
