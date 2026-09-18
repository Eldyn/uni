/**
 * @file timeFormat.ts
 * @brief Pure "M:SS" duration formatter for the turn timer, shared by every
 * consumer so a fix here (e.g. the >60s rollover) applies everywhere at once
 * instead of just wherever the timer happens to be rendered inline.
 */

/** Formats a duration in whole seconds as "M:SS" — unbounded minutes, always
 * two-digit seconds, clamped at zero. */
export function formatTurnTimer(totalSeconds: number): string {
	const clamped = Math.max(0, totalSeconds);
	const minutes = Math.floor(clamped / 60);
	const seconds = clamped % 60;
	return `${minutes}:${seconds.toString().padStart(2, "0")}`;
}
