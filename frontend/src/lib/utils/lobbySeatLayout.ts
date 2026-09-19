/**
 * @file lobbySeatLayout.ts
 * @brief Pure fit-to-box layout for the lobby's seat cards.
 *
 * Chooses the column count (and therefore the card size) that makes the
 * whole table as large as it can be while fitting the measured box, so a
 * 4-player lobby renders hero cards and a 16-player one packs into a dense
 * grid without any hand-tuned breakpoints. The result is a pure function of
 * (box, count): no DOM measurement beyond the caller's own box size.
 */

/** Width / height of a lobby seat card (trading-card proportion). */
export const CARD_ASPECT = 1 / 1.5357;

export interface SeatLayoutInput {
	/** Available box width, px. */
	boxWidth: number;
	/** Available box height, px. */
	boxHeight: number;
	/** Number of seat slots to place (max_players). */
	count: number;
	/** Gap between cards, px. */
	gap?: number;
	/** Vertical room reserved per row for the name under each card, px. */
	labelHeight?: number;
	/** Lower clamp for card width, px. */
	minCardWidth?: number;
	/** Upper clamp for card width, px (keeps small lobbies from ballooning). */
	maxCardWidth?: number;
}

export interface SeatLayout {
	cols: number;
	rows: number;
	cardWidth: number;
	cardHeight: number;
}

const DEFAULTS = {
	gap: 8,
	labelHeight: 22,
	minCardWidth: 56,
	maxCardWidth: 220
};

function clamp(value: number, low: number, high: number): number {
	return Math.min(high, Math.max(low, value));
}

/**
 * @brief Computes the densest legible grid for `count` seat cards in a box.
 * Maximises card width across every candidate column count, breaking ties
 * toward fewer rows (wider grids read better), then clamps to the size range.
 */
export function computeSeatLayout(input: SeatLayoutInput): SeatLayout {
	const gap = input.gap ?? DEFAULTS.gap;
	const labelHeight = input.labelHeight ?? DEFAULTS.labelHeight;
	const minCardWidth = input.minCardWidth ?? DEFAULTS.minCardWidth;
	const maxCardWidth = input.maxCardWidth ?? DEFAULTS.maxCardWidth;

	const count = Math.max(1, Math.floor(input.count));
	const boxWidth = Math.max(0, input.boxWidth);
	const boxHeight = Math.max(0, input.boxHeight);

	const aspect = CARD_ASPECT;
	let bestCols = 1;
	let bestWidth = minCardWidth;
	let bestScore = Number.NEGATIVE_INFINITY;

	for (let cols = 1; cols <= count; cols++) {
		const rows = Math.ceil(count / cols);
		const widthForCols = (boxWidth - gap * (cols - 1)) / cols;
		const heightForRows = (boxHeight - gap * (rows - 1) - labelHeight * rows) / rows;
		const widthForRows = heightForRows * aspect;
		const cardWidth = Math.min(widthForCols, widthForRows);
		if (cardWidth <= 0) continue;

		const score = cardWidth * (1 - 0.001 * rows);
		if (score > bestScore) {
			bestScore = score;
			bestCols = cols;
			bestWidth = cardWidth;
		}
	}

	const cardWidth = clamp(bestWidth, minCardWidth, maxCardWidth);

	return {
		cols: bestCols,
		rows: Math.ceil(count / bestCols),
		cardWidth,
		cardHeight: cardWidth / aspect
	};
}
