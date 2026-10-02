import { describe, expect, it } from "vitest";
import { buildLogLine } from "../../chat/playLog/logBuilder";
import type { LogEvent, StreakInfo } from "../../chat/playLog/logEvent";

const streak = (overrides: Partial<StreakInfo> = {}): StreakInfo => ({
	skipRun: 0,
	reverseRun: 0,
	stackedDebt: 0,
	drawRun: 0,
	totalDraws: 0,
	...overrides
});

describe("buildLogLine", () => {
	it("maps skip runs to groups and picks within the pool", () => {
		const event: LogEvent = { kind: "skip", seq: 5, player: "Ann" };
		expect(buildLogLine(event, streak({ skipRun: 1 }))?.key).toBe("log_skip");
		expect(buildLogLine(event, streak({ skipRun: 4 }))?.key).toMatch(/^log_skip_streak/);
	});

	it.each([
		[1, "log_skip"],
		[2, "log_skip_again"],
		[3, "log_skip_third"]
	])("skip run %i uses %s", (skipRun, key) => {
		expect(buildLogLine({ kind: "skip", seq: 1, player: "Ann" }, streak({ skipRun }))).toEqual({
			kind: "log",
			key,
			params: { name: "Ann" },
			tier: 0
		});
	});

	it.each([4, 7])("skip run %i uses a streak key", (skipRun) => {
		expect(buildLogLine({ kind: "skip", seq: 1, player: "Ann" }, streak({ skipRun }))?.key).toMatch(
			/^log_skip_streak/
		);
	});

	it("rotates the streak pool across different seq values (Review Focus)", () => {
		const keys = new Set(
			[1, 2, 3, 4].map(
				(seq) => buildLogLine({ kind: "skip", seq, player: "Ann" }, streak({ skipRun: 4 }))?.key
			)
		);
		expect(keys.size).toBeGreaterThan(1);
	});

	it.each([
		[1, "log_reverse"],
		[2, "log_reverse_back"],
		[3, "log_reverse_again"]
	])("reverse run %i uses %s", (reverseRun, key) => {
		expect(buildLogLine({ kind: "reverse", seq: 2 }, streak({ reverseRun }))).toMatchObject({
			key,
			tier: 0
		});
	});

	it.each([4, 9])("reverse run %i uses a spin key", (reverseRun) => {
		expect(buildLogLine({ kind: "reverse", seq: 2 }, streak({ reverseRun }))?.key).toMatch(
			/^log_reverse_spin/
		);
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
		const line = buildLogLine(event, streak({ drawRun: 2 }));
		expect(line?.key).toMatch(/^log_draw_generic/);
		expect(line?.params).toMatchObject({ name: "Ann", count: 1, handSize: 5, drawRun: 2 });
	});

	it("builds the play line with the card kind and colour", () => {
		const line = buildLogLine(
			{ kind: "play", seq: 10, player: "Ann", cardKind: "vanilla:blue_5", color: "blue" },
			streak()
		);
		expect(line).toEqual({
			kind: "log",
			key: "log_play",
			params: { name: "Ann", kind: "vanilla:blue_5", color: "blue" },
			tier: 0
		});
	});
});
