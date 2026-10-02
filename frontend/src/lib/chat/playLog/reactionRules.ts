import { stableIndex } from "./stableIndex";
import type { LogEvent, StreakInfo } from "./logEvent";

export type ReactionVariant<E> = { key: string; when?(event: E, streak: StreakInfo): boolean };

export type ReactionRule<E extends LogEvent = LogEvent> = {
	group(event: E, streak: StreakInfo): string;
	pools: Record<string, ReactionVariant<E>[]>;
	params(event: E, streak: StreakInfo): Record<string, string | number>;
};

export function selectReactionKey<E extends LogEvent>(
	ruleId: string,
	rule: ReactionRule<E>,
	event: E,
	streak: StreakInfo
): string {
	const group = rule.group(event, streak);
	const pool = rule.pools[group] ?? rule.pools.generic ?? [];
	const eligible = pool.filter((v) => !v.when || v.when(event, streak));
	const pickFrom = eligible.length > 0 ? eligible : pool;
	if (pickFrom.length === 0) return "";
	const index = stableIndex(event.seq, `${ruleId}:${group}`, pickFrom.length);
	return pickFrom[index].key;
}
