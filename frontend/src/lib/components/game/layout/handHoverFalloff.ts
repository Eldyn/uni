/**
 * Continuous exponential falloff for neighbor push displacement.
 *
 * Replaces the old linear `NEIGHBOR_PUSH_FALLOFF_CARDS` hard cutoff with
 * a smooth exponential decay: immediate neighbors get full amplitude,
 * and the push tapers off continuously so distant cards barely move.
 *
 * Formula:  sign(d) × amplitude × exp(−(|d|−1) / decay)
 *           where d = index − activeIndex, d ≠ 0.
 */

export interface HoverFalloffParams {
	amplitudeEm: number;
	decay: number;
}

/**
 * Push displacement in em for a card at `index` when the card at
 * `activeIndex` is hovered/selected. Returns 0 for the active card itself.
 *
 * Positive values push right, negative push left — matching the sign of
 * the distance from the hovered card.
 */
export function neighborPushEm(
	index: number,
	activeIndex: number,
	params: HoverFalloffParams
): number {
	const d = index - activeIndex;
	if (d === 0) return 0;
	const magnitude = params.amplitudeEm * Math.exp(-(Math.abs(d) - 1) / params.decay);
	return Math.sign(d) * magnitude;
}
