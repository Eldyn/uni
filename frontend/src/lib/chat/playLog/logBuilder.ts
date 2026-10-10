import type { LogEvent, LogLine, StreakValues } from "./logEvent";
import type { ReactionContext } from "./reactionContext";
import type { ReactionRule } from "./reactionRules";
import { selectReactions } from "./reactionRules";

export type { LogEvent, LogLine };

export const REACTION_RULES: ReactionRule[] = [
	{
		id: "play",
		match: (c) => c.event.kind === "play",
		group: () => "generic",
		pools: { generic: [{ key: "uni:log.play" }] },
		params: (c): Record<string, string | number> => {
			if (c.event.kind !== "play") return {};
			return { name: c.event.player, kind: c.event.cardKind, color: c.event.color };
		}
	},
	{
		id: "auto_play",
		match: (c) => c.event.kind === "auto_play",
		group: () => "generic",
		pools: { generic: [{ key: "uni:log.auto_play_1" }, { key: "uni:log.auto_play_2" }] },
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
			one: [{ key: "uni:log.skip" }],
			two: [{ key: "uni:log.skip_again" }, { key: "uni:log.skip_again_2" }],
			three: [{ key: "uni:log.skip_third" }],
			streak: [{ key: "uni:log.skip_streak" }, { key: "uni:log.skip_streak_2" }]
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
			one: [{ key: "uni:log.reverse" }],
			two: [{ key: "uni:log.reverse_back" }, { key: "uni:log.reverse_back_2" }],
			three: [{ key: "uni:log.reverse_again" }],
			streak: [{ key: "uni:log.reverse_spin" }, { key: "uni:log.reverse_spin_2" }]
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
			first: [{ key: "uni:log.draw_first_1" }, { key: "uni:log.draw_first_2" }],
			generic: [
				{ key: "uni:log.draw_generic_1" },
				{ key: "uni:log.draw_generic_2" },
				{ key: "uni:log.draw_generic_3" }
			],
			many: [{ key: "uni:log.draw_many_1" }, { key: "uni:log.draw_many_2" }],
			big_hand: [{ key: "uni:log.draw_big_hand_1" }, { key: "uni:log.draw_big_hand_2" }],
			large: [{ key: "uni:log.draw_large_1" }],
			penalty_small: [{ key: "uni:log.draw_stack" }],
			penalty_heavy: [{ key: "uni:log.draw_stack_heavy" }]
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
		match: (c) => c.event.kind === "blocked" && !!c.event.blocker,
		group: () => "generic",
		pools: { generic: [{ key: "uni:log.blocked_1" }, { key: "uni:log.blocked_2" }] },
		params: (c) => ({
			blocker: c.event.kind === "blocked" ? (c.event.blocker ?? "") : "",
			target: c.event.kind === "blocked" ? c.event.player : ""
		})
	},
	{
		id: "reshuffle",
		match: (c) => c.event.kind === "reshuffle",
		group: () => "generic",
		pools: {
			generic: [
				{ key: "uni:log.reshuffle_1" },
				{ key: "uni:log.reshuffle_2" },
				{ key: "uni:log.reshuffle_3" }
			]
		},
		params: () => ({})
	},
	{
		id: "keep",
		match: (c) => c.event.kind === "keep",
		group: () => "generic",
		pools: { generic: [{ key: "uni:log.keep_drawn" }] },
		// INFO: the kept card's identity never reaches the log — a keep line
		//       names only the player.
		params: (c) => ({ name: c.event.kind === "keep" ? c.event.player : "" })
	},
	{
		id: "near_win",
		match: (c) => c.event.kind === "near_win",
		group: () => "generic",
		pools: {
			generic: [
				{ key: "uni:log.near_win_1" },
				{ key: "uni:log.near_win_2" },
				{ key: "uni:log.near_win_3" }
			]
		},
		params: (c) => ({ name: c.event.kind === "near_win" ? c.event.player : "" })
	},
	{
		id: "win",
		match: (c) => c.event.kind === "win",
		group: () => "generic",
		pools: { generic: [{ key: "uni:log.win_1" }, { key: "uni:log.win_2" }] },
		params: (c) => ({ name: c.event.kind === "win" ? c.event.player : "" })
	},
	{
		id: "elimination",
		match: (c) => c.event.kind === "elimination",
		group: (c) => (c.event.kind === "elimination" && c.event.finished ? "finished" : "left_behind"),
		pools: {
			finished: [{ key: "uni:log.finish_1" }, { key: "uni:log.finish_2" }],
			left_behind: [{ key: "uni:log.left_behind_1" }, { key: "uni:log.left_behind_2" }]
		},
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
			first: [{ key: "uni:log.wild_reaction_1" }, { key: "uni:log.wild_reaction_first_2" }],
			second: [
				{ key: "uni:log.wild_reaction_second_1" },
				{ key: "uni:log.wild_reaction_second_2" }
			],
			third: [{ key: "uni:log.wild_reaction_third_1" }, { key: "uni:log.wild_reaction_third_2" }]
		},
		params: (c) => ({
			name: c.event.kind === "wild" ? c.event.player : "",
			color: c.event.kind === "wild" ? c.event.color : "",
			prevColor: c.prevWildColor ?? ""
		})
	}
];

export function buildLogLines(event: LogEvent, streak: StreakValues): LogLine[] {
	const ctx: ReactionContext = { seq: event.seq, event, ...streak };
	return selectReactions(ctx, REACTION_RULES).map((r) => ({
		kind: "log",
		key: r.key,
		params: r.params,
		tier: 0
	}));
}
