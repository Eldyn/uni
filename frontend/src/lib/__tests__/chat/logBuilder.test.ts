import { describe, expect, it } from "vitest";
import { buildLogLines } from "../../chat/playLog/logBuilder";
import type { LogEvent, StreakInfo } from "../../chat/playLog/logEvent";

const streak = (overrides: Partial<StreakInfo> = {}): StreakInfo => ({
	skipRun: 0,
	reverseRun: 0,
	drawRun: 0,
	totalDraws: 0,
	wildRun: 0,
	lastWildColor: null,
	prevWildColor: null,
	nearWinTarget: null,
	nearWinOpen: false,
	...overrides
});

const keyOf = (event: LogEvent, info: StreakInfo = streak()): string =>
	buildLogLines(event, info)[0]?.key ?? "";

describe("buildLogLines", () => {
	it("maps skip runs to groups and picks within the pool", () => {
		const event: LogEvent = { kind: "skip", seq: 5, player: "Ann" };
		expect(keyOf(event, streak({ skipRun: 1 }))).toBe("log_skip");
		expect(keyOf(event, streak({ skipRun: 4 }))).toMatch(/^log_skip_streak/);
	});

	it.each([
		[1, "log_skip"],
		[2, "log_skip_again_2"],
		[3, "log_skip_third"]
	])("skip run %i uses %s", (skipRun, key) => {
		expect(buildLogLines({ kind: "skip", seq: 1, player: "Ann" }, streak({ skipRun }))[0]).toEqual({
			kind: "log",
			key,
			params: { name: "Ann" },
			tier: 0
		});
	});

	it.each([4, 7])("skip run %i uses a streak key", (skipRun) => {
		expect(keyOf({ kind: "skip", seq: 1, player: "Ann" }, streak({ skipRun }))).toMatch(
			/^log_skip_streak/
		);
	});

	it("rotates the streak pool across different seq values (Review Focus)", () => {
		const keys = new Set(
			[1, 2, 3, 4].map((seq) => keyOf({ kind: "skip", seq, player: "Ann" }, streak({ skipRun: 4 })))
		);
		expect(keys.size).toBeGreaterThan(1);
	});

	it.each([
		[1, "log_reverse"],
		[2, "log_reverse_back_2"],
		[3, "log_reverse_again"]
	])("reverse run %i uses %s", (reverseRun, key) => {
		expect(buildLogLines({ kind: "reverse", seq: 2 }, streak({ reverseRun }))[0]).toMatchObject({
			key,
			tier: 0
		});
	});

	it.each([4, 9])("reverse run %i uses a spin key", (reverseRun) => {
		expect(keyOf({ kind: "reverse", seq: 2 }, streak({ reverseRun }))).toMatch(/^log_reverse_spin/);
	});

	it("builds a draw reaction with the live draw run", () => {
		const event: LogEvent = {
			kind: "draw",
			seq: 4,
			player: "Ann",
			count: 1,
			penalty: false,
			handSize: 5
		};
		const line = buildLogLines(event, streak({ drawRun: 2 }))[0];
		expect(line?.key).toMatch(/^log_draw_generic/);
		expect(line?.params).toMatchObject({ name: "Ann", count: 1, handSize: 5, drawRun: 2 });
	});

	it("builds the play line with the card kind and colour", () => {
		const line = buildLogLines(
			{ kind: "play", seq: 10, player: "Ann", cardKind: "vanilla:blue_5", color: "blue" },
			streak()
		)[0];
		expect(line).toEqual({
			kind: "log",
			key: "log_play",
			params: { name: "Ann", kind: "vanilla:blue_5", color: "blue" },
			tier: 0
		});
	});
});

describe("draw reaction groups", () => {
	const draw = (over: Partial<Extract<LogEvent, { kind: "draw" }>> = {}): LogEvent => ({
		kind: "draw",
		seq: 1,
		player: "Ann",
		count: 1,
		penalty: false,
		...over
	});
	const withDraws = (drawRun: number, totalDraws: number): StreakInfo =>
		streak({ drawRun, totalDraws });

	it.each([
		["penalty_small", draw({ penalty: true, total: 4 }), streak(), "log_draw_stack"],
		["penalty_heavy", draw({ penalty: true, total: 8 }), streak(), "log_draw_stack_heavy"],
		["big_hand", draw({ handSize: 10 }), withDraws(1, 1), /^log_draw_big_hand/],
		["large", draw({ count: 4, handSize: 5 }), withDraws(1, 1), "log_draw_large_1"],
		["first", draw(), withDraws(1, 1), /^log_draw_first/],
		["many", draw(), withDraws(4, 5), /^log_draw_many/],
		["generic", draw(), withDraws(2, 3), /^log_draw_generic/]
	])("group %s picks %s", (_group, event, info, expected) => {
		const key = keyOf(event as LogEvent, info as StreakInfo);
		if (typeof expected === "string") expect(key).toBe(expected);
		else expect(key).toMatch(expected as RegExp);
	});

	it("scopes the first-draw joke to the match, not the player", () => {
		expect(keyOf(draw(), withDraws(1, 1))).toMatch(/^log_draw_first/);
		expect(keyOf(draw(), withDraws(1, 2))).toMatch(/^log_draw_generic/);
	});

	it("prefers penalty over hand size and count", () => {
		expect(keyOf(draw({ penalty: true, total: 4, handSize: 12, count: 4 }))).toBe("log_draw_stack");
		expect(keyOf(draw({ penalty: true, total: 9, handSize: 12, count: 4 }))).toBe(
			"log_draw_stack_heavy"
		);
	});

	it("prefers big hand over large count", () => {
		expect(keyOf(draw({ handSize: 10, count: 4 }), streak({ drawRun: 1 }))).toMatch(
			/^log_draw_big_hand/
		);
	});

	it("carries the live drawRun and victim into params (R3)", () => {
		const line = buildLogLines(
			draw({ count: 2, handSize: 5, victim: "Bob" }),
			streak({ drawRun: 3 })
		)[0];
		expect(line?.params).toMatchObject({
			name: "Ann",
			count: 2,
			handSize: 5,
			drawRun: 3,
			victim: "Bob"
		});
	});
});
