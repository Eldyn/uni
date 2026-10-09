import type { StreakValues } from "./logEvent";

/** The `context` object of a client event envelope: streak values by id. */
export type EventContext = Readonly<Record<string, unknown>>;

export const SKIP_RUN_STREAK_ID = "uni:skip_run";
export const REVERSE_RUN_STREAK_ID = "uni:reverse_run";
export const DRAW_RUN_STREAK_ID = "uni:draw_run";
export const TOTAL_DRAWS_STREAK_ID = "uni:total_draws";
export const WILD_RUN_STREAK_ID = "uni:wild_run";
export const LAST_WILD_COLOR_STREAK_ID = "uni:last_wild_color";

/** The values for an event whose envelope has no context. */
export const NO_STREAKS: StreakValues = {
	skipRun: 0,
	reverseRun: 0,
	drawRun: 0,
	totalDraws: 0,
	wildRun: 0,
	lastWildColor: null,
	prevWildColor: null
};

function count(value: unknown): number {
	return typeof value === "number" && Number.isInteger(value) && value >= 0 ? value : 0;
}

function color(value: unknown): string | null {
	return typeof value === "string" && value !== "" ? value : null;
}

function record(value: unknown): Record<string, unknown> | null {
	return typeof value === "object" && value !== null && !Array.isArray(value)
		? (value as Record<string, unknown>)
		: null;
}

/**
 * Reads the streak values of an envelope `context`. The server computes all
 * of them; the client never counts. A missing or malformed value reads as
 * "no streak info": zero, or no color. `seat` selects the entry of the
 * per-seat draw count.
 */
export function readStreaks(context: EventContext | undefined, seat?: string): StreakValues {
	if (context === undefined) return NO_STREAKS;
	const drawRuns = record(context[DRAW_RUN_STREAK_ID]);
	const wildColors = record(context[LAST_WILD_COLOR_STREAK_ID]);
	return {
		skipRun: count(context[SKIP_RUN_STREAK_ID]),
		reverseRun: count(context[REVERSE_RUN_STREAK_ID]),
		drawRun: seat !== undefined && drawRuns !== null ? count(drawRuns[seat]) : 0,
		totalDraws: count(context[TOTAL_DRAWS_STREAK_ID]),
		wildRun: count(context[WILD_RUN_STREAK_ID]),
		lastWildColor: wildColors === null ? null : color(wildColors.last),
		prevWildColor: wildColors === null ? null : color(wildColors.previous)
	};
}
