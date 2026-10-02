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
			streak: [{ key: "log_skip_streak" }, { key: "log_skip_streak_2" }]
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
			streak: [{ key: "log_reverse_spin" }, { key: "log_reverse_spin_2" }]
		},
		params: () => ({})
	},
	auto_play: {
		group: () => "generic",
		pools: { generic: [{ key: "log_auto_play_1" }, { key: "log_auto_play_2" }] },
		params: (e) => ({ name: e.player, kind: e.cardKind, color: e.color })
	},
	reshuffle: {
		group: () => "generic",
		pools: { generic: [{ key: "log_reshuffle_1" }, { key: "log_reshuffle_2" }] },
		params: () => ({})
	},
	wild: {
		group: () => "generic",
		pools: { generic: [{ key: "log_wild_reaction_1" }] },
		params: (e) => ({ name: e.player, color: e.color })
	},
	near_win: {
		group: () => "generic",
		pools: { generic: [{ key: "log_near_win_1" }, { key: "log_near_win_2" }] },
		params: (e) => ({ name: e.player })
	},
	win: {
		group: () => "generic",
		pools: { generic: [{ key: "log_win_1" }, { key: "log_win_2" }] },
		params: (e) => ({ name: e.player })
	},
	elimination: {
		group: () => "generic",
		pools: { generic: [{ key: "log_elimination_1" }, { key: "log_elimination_2" }] },
		params: (e) => ({ name: e.player, place: e.place })
	},
	draw: {
		group: (e, s) => {
			if (e.penalty) return (e.total ?? 0) >= 8 ? "penalty_heavy" : "penalty_small";
			if ((e.handSize ?? 0) >= 10) return "big_hand";
			if (e.count >= 4) return "large";
			if (s.totalDraws === 1) return "first";
			if (s.drawRun >= 4) return "many";
			return "generic";
		},
		pools: {
			first: [{ key: "log_draw_first_1" }, { key: "log_draw_first_2" }],
			generic: [{ key: "log_draw_generic_1" }, { key: "log_draw_generic_2" }],
			many: [{ key: "log_draw_many_1" }, { key: "log_draw_many_2" }],
			big_hand: [{ key: "log_draw_big_hand_1" }, { key: "log_draw_big_hand_2" }],
			large: [{ key: "log_draw_large_1" }],
			penalty_small: [{ key: "log_draw_stack" }],
			penalty_heavy: [{ key: "log_draw_stack_heavy" }]
		},
		params: (e, s) => ({
			name: e.player,
			count: e.count,
			amount: e.amount ?? 0,
			total: e.total ?? 0,
			handSize: e.handSize ?? 0,
			drawRun: s.drawRun,
			victim: e.victim ?? e.player
		})
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
