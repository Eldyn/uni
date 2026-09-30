import type { LogEvent, LogLine, StreakInfo } from "./logEvent";

export type { LogEvent, LogLine };

export const SKIP_AGAIN_RUN = 2;
export const SKIP_THIRD_RUN = 3;
export const REVERSE_BACK_RUN = 2;
export const REVERSE_AGAIN_RUN = 3;

type Params = Record<string, string | number>;

export type LogRule<E extends LogEvent = LogEvent> = {
	run: (streak: StreakInfo) => number;
	variants: { minRun: number; key: string }[];
	// INFO: method syntax keeps params bivariant so a narrow rule fits LogRule
	params(event: E): Params;
};

type RuleKind = Exclude<LogEvent["kind"], "play">;

const SINGLE_RUN = () => 1;

export const LOG_RULES = {
	skip: {
		run: (streak) => streak.skipRun,
		variants: [
			{ minRun: 1, key: "log_skip" },
			{ minRun: SKIP_AGAIN_RUN, key: "log_skip_again" },
			{ minRun: SKIP_THIRD_RUN, key: "log_skip_third" }
		],
		params: (event) => ({ name: event.player })
	},
	reverse: {
		run: (streak) => streak.reverseRun,
		variants: [
			{ minRun: 1, key: "log_reverse" },
			{ minRun: REVERSE_BACK_RUN, key: "log_reverse_back" },
			{ minRun: REVERSE_AGAIN_RUN, key: "log_reverse_again" }
		],
		params: () => ({})
	},
	draw_stack: {
		run: SINGLE_RUN,
		variants: [{ minRun: 1, key: "log_draw_stack" }],
		params: (event) => ({
			name: event.player,
			victim: event.victim,
			amount: event.amount,
			total: event.total
		})
	},
	wild: {
		run: SINGLE_RUN,
		variants: [{ minRun: 1, key: "log_wild" }],
		params: (event) => ({ name: event.player, color: event.color })
	}
} satisfies { [K in RuleKind]: LogRule<Extract<LogEvent, { kind: K }>> };

const RULES_BY_KIND: Partial<Record<string, LogRule>> = LOG_RULES;

export function buildLogLine(event: LogEvent, streak: StreakInfo): LogLine | null {
	const rule = RULES_BY_KIND[event.kind];
	if (!rule) return null;
	const run = rule.run(streak);
	const ascending = [...rule.variants].sort((a, b) => a.minRun - b.minRun);
	let tier = 0;
	ascending.forEach((variant, index) => {
		if (run >= variant.minRun) tier = index;
	});
	return {
		kind: "log",
		key: ascending[tier].key,
		params: rule.params(event),
		tier
	};
}
