import type { StreakInfo } from "./logEvent";

/** The `context` object of a client event envelope: streak values by id. */
export type EventContext = Readonly<Record<string, unknown>>;

export const SKIP_RUN_STREAK_ID = "uni:skip_run";
export const REVERSE_RUN_STREAK_ID = "uni:reverse_run";

function runValue(context: EventContext | undefined, streakId: string): number | undefined {
	const value = context?.[streakId];
	return typeof value === "number" && Number.isInteger(value) && value >= 0 ? value : undefined;
}

/**
 * Replaces the local `skipRun` and `reverseRun` with the server values when
 * the envelope `context` holds them. A missing or malformed value keeps the
 * local value, so a server without the streak files still works.
 */
export function withServerStreaks(
	local: StreakInfo,
	context: EventContext | undefined
): StreakInfo {
	const skipRun = runValue(context, SKIP_RUN_STREAK_ID);
	const reverseRun = runValue(context, REVERSE_RUN_STREAK_ID);
	if (skipRun === undefined && reverseRun === undefined) return local;
	return {
		...local,
		skipRun: skipRun ?? local.skipRun,
		reverseRun: reverseRun ?? local.reverseRun
	};
}
