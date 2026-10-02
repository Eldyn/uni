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
		[3, "log_skip_third"],
		[4, "log_skip_streak"],
		[7, "log_skip_streak"]
	])("skip run %i uses %s", (skipRun, key) => {
		expect(buildLogLine({ kind: "skip", seq: 1, player: "Ann" }, streak({ skipRun }))).toEqual({
			kind: "log",
			key,
			params: { name: "Ann" },
			tier: 0
		});
	});

	it.each([
		[1, "log_reverse"],
		[2, "log_reverse_back"],
		[3, "log_reverse_again"],
		[4, "log_reverse_spin"],
		[9, "log_reverse_spin"]
	])("reverse run %i uses %s", (reverseRun, key) => {
		expect(buildLogLine({ kind: "reverse", seq: 2 }, streak({ reverseRun }))).toMatchObject({
			key,
			tier: 0
		});
	});

	it("builds the temporary draw stack line with the running total", () => {
		const event: LogEvent = {
			kind: "draw_stack",
			seq: 4,
			player: "Ann",
			victim: "Bob",
			amount: 2,
			total: 6
		};
		const line = buildLogLine(event, streak({ stackedDebt: 6 }));
		expect(line?.key).toBe("log_draw_stack");
		expect(line?.params).toEqual({
			name: "Ann",
			victim: "Bob",
			amount: 2,
			total: 6
		});
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
