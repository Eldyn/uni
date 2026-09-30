import { describe, expect, it } from "vitest";
import { buildLogLine } from "../../chat/playLog/logBuilder";
import type { LogEvent, StreakInfo } from "../../chat/playLog/logEvent";

const streak = (overrides: Partial<StreakInfo> = {}): StreakInfo => ({
	skipRun: 0,
	reverseRun: 0,
	stackedDebt: 0,
	...overrides
});

describe("buildLogLine", () => {
	const skip: LogEvent = { kind: "skip", player: "Ann" };

	it.each([
		[1, "log_skip", 0],
		[2, "log_skip_again", 1],
		[3, "log_skip_third", 2],
		[7, "log_skip_third", 2]
	])("skip run %i uses %s", (skipRun, key, tier) => {
		expect(buildLogLine(skip, streak({ skipRun }))).toEqual({
			kind: "log",
			key,
			params: { name: "Ann" },
			tier
		});
	});

	it.each([
		[1, "log_reverse"],
		[2, "log_reverse_back"],
		[3, "log_reverse_again"],
		[5, "log_reverse_again"]
	])("reverse run %i uses %s", (reverseRun, key) => {
		expect(buildLogLine({ kind: "reverse" }, streak({ reverseRun }))?.key).toBe(key);
	});

	it("builds the draw stack line with the running total", () => {
		const event: LogEvent = {
			kind: "draw_stack",
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

	it("builds the wild line with the chosen colour", () => {
		const line = buildLogLine({ kind: "wild", player: "Ann", color: "red" }, streak());
		expect(line?.key).toBe("log_wild");
		expect(line?.params).toEqual({ name: "Ann", color: "red" });
	});

	it("returns null for events without a log rule", () => {
		expect(buildLogLine({ kind: "play", player: "Ann" }, streak())).toBeNull();
	});
});
