import type { LogEvent, LogLine, StreakInfo } from "./logEvent";
import { selectReactionKey, type ReactionRule } from "./reactionRules";

export type { LogEvent, LogLine };

export const REACTION_RULES = {
	play: {
		group: () => "generic",
		pools: { generic: [{ key: "log_play" }] },
		params: (e) => ({ name: e.player, kind: e.cardKind, color: e.color })
	},
	skip: {
		group: (_e, s) =>
			s.skipRun >= 4 ? "streak" : s.skipRun === 3 ? "three" : s.skipRun === 2 ? "two" : "one",
		pools: {
			one: [{ key: "log_skip" }],
			two: [{ key: "log_skip_again" }],
			three: [{ key: "log_skip_third" }],
			streak: [{ key: "log_skip_streak" }]
		},
		params: (e) => ({ name: e.player })
	},
	reverse: {
		group: (_e, s) =>
			s.reverseRun >= 4
				? "streak"
				: s.reverseRun === 3
					? "three"
					: s.reverseRun === 2
						? "two"
						: "one",
		pools: {
			one: [{ key: "log_reverse" }],
			two: [{ key: "log_reverse_back" }],
			three: [{ key: "log_reverse_again" }],
			streak: [{ key: "log_reverse_spin" }]
		},
		params: () => ({})
	},
	draw_stack: {
		// INFO: temporary rule until the draw event and its pools exist
		group: () => "generic",
		pools: { generic: [{ key: "log_draw_stack" }] },
		params: (e) => ({ name: e.player, victim: e.victim, amount: e.amount, total: e.total })
	}
} satisfies { [K in LogEvent["kind"]]: ReactionRule<Extract<LogEvent, { kind: K }>> };

const RULES_BY_KIND: Partial<Record<string, ReactionRule>> = REACTION_RULES;

export function buildLogLine(event: LogEvent, streak: StreakInfo): LogLine | null {
	const rule = RULES_BY_KIND[event.kind];
	if (!rule) return null;
	const key = selectReactionKey(event.kind, rule, event, streak);
	if (!key) return null;
	return { kind: "log", key, params: rule.params(event, streak), tier: 0 };
}
