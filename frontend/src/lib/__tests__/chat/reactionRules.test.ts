import { describe, expect, it } from "vitest";
import { selectReactionKey, type ReactionRule } from "../../chat/playLog/reactionRules";
import { buildLogLine } from "../../chat/playLog/logBuilder";
import type { LogEvent, StreakInfo } from "../../chat/playLog/logEvent";

type E = { kind: "x"; seq: number; n: number };
const streak: StreakInfo = {
	skipRun: 0,
	reverseRun: 0,
	stackedDebt: 0,
	drawRun: 0,
	totalDraws: 0
};

const rule: ReactionRule<E> = {
	group: (e) => (e.n >= 4 ? "many" : "few"),
	pools: {
		few: [{ key: "few_1" }, { key: "few_2" }],
		many: [{ key: "many_1" }, { key: "many_2" }]
	},
	params: () => ({})
};

describe("selectReactionKey", () => {
	it("picks from the group pool deterministically", () => {
		const a = selectReactionKey("x", rule, { kind: "x", seq: 9, n: 4 }, streak);
		const b = selectReactionKey("x", rule, { kind: "x", seq: 9, n: 4 }, streak);
		expect(a).toBe(b);
		expect(["many_1", "many_2"]).toContain(a);
	});

	it("uses when to filter the pool", () => {
		const gated: ReactionRule<E> = {
			...rule,
			pools: { many: [{ key: "only", when: (e) => e.n === 5 }, { key: "fallback" }] }
		};
		expect(selectReactionKey("x", gated, { kind: "x", seq: 1, n: 4 }, streak)).toBe("fallback");
	});

	it("falls back to the first pool entry when the filter empties it", () => {
		const empty: ReactionRule<E> = {
			...rule,
			pools: { many: [{ key: "never", when: () => false }] }
		};
		expect(selectReactionKey("x", empty, { kind: "x", seq: 1, n: 4 }, streak)).toBe("never");
	});

	it("falls back to generic when the group is unknown", () => {
		const g: ReactionRule<E> = {
			...rule,
			group: () => "missing",
			pools: { generic: [{ key: "g" }] }
		};
		expect(selectReactionKey("x", g, { kind: "x", seq: 1, n: 1 }, streak)).toBe("g");
	});
});

describe("draw reaction groups", () => {
	const withDrawRun = (drawRun: number): StreakInfo => ({ ...streak, drawRun });
	const draw = (over: Partial<Extract<LogEvent, { kind: "draw" }>> = {}): LogEvent => ({
		kind: "draw",
		seq: 1,
		player: "Ann",
		count: 1,
		penalty: false,
		...over
	});
	const keyOf = (event: LogEvent, info: StreakInfo = streak) =>
		buildLogLine(event, info)?.key ?? "";

	it.each([
		["penalty_small", draw({ penalty: true, total: 4 }), streak, "log_draw_stack"],
		["penalty_heavy", draw({ penalty: true, total: 8 }), streak, "log_draw_stack_heavy"],
		["big_hand", draw({ handSize: 10 }), withDrawRun(1), /^log_draw_big_hand/],
		["large", draw({ count: 4, handSize: 5 }), withDrawRun(1), "log_draw_large_1"],
		["first", draw(), withDrawRun(1), /^log_draw_first/],
		["many", draw(), withDrawRun(4), /^log_draw_many/],
		["generic", draw(), withDrawRun(2), /^log_draw_generic/]
	])("group %s picks %s", (_group, event, info, expected) => {
		const key = keyOf(event as LogEvent, info as StreakInfo);
		if (typeof expected === "string") expect(key).toBe(expected);
		else expect(key).toMatch(expected as RegExp);
	});

	it("prefers penalty over hand size and count", () => {
		expect(keyOf(draw({ penalty: true, total: 4, handSize: 12, count: 4 }))).toBe("log_draw_stack");
		expect(keyOf(draw({ penalty: true, total: 9, handSize: 12, count: 4 }))).toBe(
			"log_draw_stack_heavy"
		);
	});

	it("prefers big hand over large count", () => {
		expect(keyOf(draw({ handSize: 10, count: 4 }), withDrawRun(1))).toMatch(/^log_draw_big_hand/);
	});

	it("carries the live drawRun and victim into params (R3)", () => {
		const line = buildLogLine(draw({ count: 2, handSize: 5, victim: "Bob" }), withDrawRun(3));
		expect(line?.params).toMatchObject({
			name: "Ann",
			count: 2,
			handSize: 5,
			drawRun: 3,
			victim: "Bob"
		});
	});
});

describe("reshuffle, wild, auto, near-win and game-end rules", () => {
	const keyOf = (event: LogEvent, info: StreakInfo = streak) =>
		buildLogLine(event, info)?.key ?? "";

	it("picks a reshuffle joke", () => {
		expect(keyOf({ kind: "reshuffle", seq: 1 })).toMatch(/^log_reshuffle/);
	});

	it("carries the wild picker and colour", () => {
		const line = buildLogLine({ kind: "wild", seq: 2, player: "Ann", color: "red" }, streak);
		expect(line?.key).toBe("log_wild_reaction_1");
		expect(line?.params).toEqual({ name: "Ann", color: "red" });
	});

	it("carries the auto-played card kind and colour", () => {
		const line = buildLogLine(
			{ kind: "auto_play", seq: 3, player: "Ann", cardKind: "vanilla:wild", color: "blue" },
			streak
		);
		expect(line?.key).toMatch(/^log_auto_play/);
		expect(line?.params).toEqual({ name: "Ann", kind: "vanilla:wild", color: "blue" });
	});

	it("names the near-win player", () => {
		const line = buildLogLine({ kind: "near_win", seq: 4, player: "Ann" }, streak);
		expect(line?.key).toMatch(/^log_near_win/);
		expect(line?.params).toEqual({ name: "Ann" });
	});

	it("names the winner", () => {
		const line = buildLogLine({ kind: "win", seq: 1, player: "Ann" }, streak);
		expect(line?.key).toMatch(/^log_win/);
		expect(line?.params).toEqual({ name: "Ann" });
	});

	it("carries the eliminated player and place", () => {
		const line = buildLogLine({ kind: "elimination", seq: 2, player: "Ann", place: 2 }, streak);
		expect(line?.key).toMatch(/^log_elimination/);
		expect(line?.params).toEqual({ name: "Ann", place: 2 });
	});
});
