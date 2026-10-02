import type { LogEvent, LogLine, StreakInfo } from "./logEvent";
import type { ReactionContext } from "./reactionContext";
import type { ReactionRule } from "./reactionRules";
import { selectReactions } from "./reactionRules";

export type { LogEvent, LogLine };

export const REACTION_RULES: ReactionRule[] = [
	{
		id: "play",
		match: (c) => c.event.kind === "play",
		group: () => "generic",
		pools: { generic: [{ key: "log_play" }] },
		params: (c): Record<string, string | number> => {
			if (c.event.kind !== "play") return {};
			return { name: c.event.player, kind: c.event.cardKind, color: c.event.color };
		}
	},
	{
		id: "auto_play",
		match: (c) => c.event.kind === "auto_play",
		group: () => "generic",
		pools: { generic: [{ key: "log_auto_play_1" }, { key: "log_auto_play_2" }] },
		params: (c): Record<string, string | number> => {
			if (c.event.kind !== "auto_play") return {};
			return { name: c.event.player, kind: c.event.cardKind, color: c.event.color };
		}
	},
	{
		id: "skip",
		match: (c) => c.event.kind === "skip",
		group: (c) =>
			c.skipRun >= 4 ? "streak" : c.skipRun === 3 ? "three" : c.skipRun === 2 ? "two" : "one",
		pools: {
			one: [{ key: "log_skip" }],
			two: [{ key: "log_skip_again" }],
			three: [{ key: "log_skip_third" }],
			streak: [{ key: "log_skip_streak" }, { key: "log_skip_streak_2" }]
		},
		params: (c) => ({ name: c.event.kind === "skip" ? c.event.player : "" })
	},
	{
		id: "reverse",
		match: (c) => c.event.kind === "reverse",
		group: (c) =>
			c.reverseRun >= 4
				? "streak"
				: c.reverseRun === 3
					? "three"
					: c.reverseRun === 2
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
	{
		id: "draw",
		match: (c) => c.event.kind === "draw",
		group: (c) => {
			if (c.event.kind !== "draw") return "generic";
			const e = c.event;
			if (e.penalty) return (e.total ?? 0) >= 8 ? "penalty_heavy" : "penalty_small";
			if ((e.handSize ?? 0) >= 10) return "big_hand";
			if (e.count >= 4) return "large";
			if (c.totalDraws === 1) return "first";
			if (c.drawRun >= 4) return "many";
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
		params: (c): Record<string, string | number> => {
			if (c.event.kind !== "draw") return {};
			return {
				name: c.event.player,
				victim: c.event.victim ?? c.event.player,
				count: c.event.count,
				amount: c.event.amount ?? 0,
				total: c.event.total ?? 0,
				handSize: c.event.handSize ?? 0,
				drawRun: c.drawRun
			};
		}
	},
	{
		id: "blocked",
		match: (c) => {
			if (!c.nearWinOpen || c.nearWinTarget === null) return false;
			const e = c.event;
			if (e.kind === "skip") return e.player === c.nearWinTarget && !!e.actor;
			if (e.kind === "draw" && e.penalty) return e.victim === c.nearWinTarget && !!e.actor;
			return false;
		},
		group: () => "generic",
		pools: { generic: [{ key: "log_blocked_1" }, { key: "log_blocked_2" }] },
		params: (c) => {
			const e = c.event;
			const blocker = e.kind === "skip" || e.kind === "draw" ? (e.actor ?? "") : "";
			return { blocker, target: c.nearWinTarget ?? "" };
		}
	},
	{
		id: "reshuffle",
		match: (c) => c.event.kind === "reshuffle",
		group: () => "generic",
		pools: { generic: [{ key: "log_reshuffle_1" }, { key: "log_reshuffle_2" }] },
		params: () => ({})
	},
	{
		id: "near_win",
		match: (c) => c.event.kind === "near_win",
		group: () => "generic",
		pools: { generic: [{ key: "log_near_win_1" }, { key: "log_near_win_2" }] },
		params: (c) => ({ name: c.event.kind === "near_win" ? c.event.player : "" })
	},
	{
		id: "win",
		match: (c) => c.event.kind === "win",
		group: () => "generic",
		pools: { generic: [{ key: "log_win_1" }, { key: "log_win_2" }] },
		params: (c) => ({ name: c.event.kind === "win" ? c.event.player : "" })
	},
	{
		id: "elimination",
		match: (c) => c.event.kind === "elimination",
		group: () => "generic",
		pools: { generic: [{ key: "log_elimination_1" }, { key: "log_elimination_2" }] },
		params: (c): Record<string, string | number> => {
			if (c.event.kind !== "elimination") return {};
			return { name: c.event.player, place: c.event.place };
		}
	},
	{
		id: "wild",
		match: (c) => c.event.kind === "wild",
		group: (c) => (c.wildRun <= 1 ? "first" : c.wildRun === 2 ? "second" : "third"),
		pools: {
			first: [{ key: "log_wild_reaction_1" }, { key: "log_wild_reaction_first_2" }],
			second: [{ key: "log_wild_reaction_second_1" }, { key: "log_wild_reaction_second_2" }],
			third: [{ key: "log_wild_reaction_third_1" }, { key: "log_wild_reaction_third_2" }]
		},
		params: (c) => ({
			name: c.event.kind === "wild" ? c.event.player : "",
			color: c.event.kind === "wild" ? c.event.color : "",
			prevColor: c.prevWildColor ?? ""
		})
	}
];

export function buildLogLines(event: LogEvent, streak: StreakInfo): LogLine[] {
	const ctx: ReactionContext = { seq: event.seq, event, ...streak };
	return selectReactions(ctx, REACTION_RULES).map((r) => ({
		kind: "log",
		key: r.key,
		params: r.params,
		tier: 0
	}));
}
