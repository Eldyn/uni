import type { LogEvent, LogLine, StreakInfo } from "./logEvent";

export type { LogEvent, LogLine };

export const SKIP_AGAIN_RUN = 2;
export const SKIP_THIRD_RUN = 3;
export const REVERSE_BACK_RUN = 2;
export const REVERSE_AGAIN_RUN = 3;

type Params = Record<string, string | number>;

export type LogRule = {
	run: (streak: StreakInfo) => number;
	variants: { minRun: number; key: string }[];
	params: (event: never) => Params;
};

type RuleFor<K extends LogEvent["kind"]> = {
	run: (streak: StreakInfo) => number;
	variants: { minRun: number; key: string }[];
	params: (event: Extract<LogEvent, { kind: K }>) => Params;
};

const SINGLE_RUN = () => 1;

export const LOG_RULES: {
	[K in Exclude<LogEvent["kind"], "play">]: RuleFor<K>;
} = {
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
};

export function buildLogLine(event: LogEvent, streak: StreakInfo): LogLine | null {
	const rule = (LOG_RULES as Record<string, LogRule | undefined>)[event.kind];
	if (!rule) return null;
	const run = rule.run(streak);
	let tier = 0;
	rule.variants.forEach((variant, index) => {
		if (run >= variant.minRun) tier = index;
	});
	return {
		kind: "log",
		key: rule.variants[tier].key,
		params: rule.params(event as never),
		tier
	};
}
