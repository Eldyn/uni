import { describe, expect, it } from "vitest";
import { buildLogLine, LOG_RULES } from "../../chat/playLog/logBuilder";
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
		[4, "log_skip_streak", 3],
		[7, "log_skip_streak", 3]
	])("skip run %i uses %s", (skipRun, key, tier) => {
		expect(buildLogLine(skip, streak({ skipRun }))).toEqual({
			kind: "log",
			key,
			params: { name: "Ann" },
			tier
		});
	});

	it.each([
		[1, "log_reverse", 0],
		[2, "log_reverse_back", 1],
		[3, "log_reverse_again", 2],
		[4, "log_reverse_spin", 3],
		[9, "log_reverse_spin", 3]
	])("reverse run %i uses %s", (reverseRun, key, tier) => {
		expect(buildLogLine({ kind: "reverse" }, streak({ reverseRun }))).toMatchObject({
			key,
			tier
		});
	});

	it.each([
		[2, "log_draw_stack", 0],
		[7, "log_draw_stack", 0],
		[8, "log_draw_stack_heavy", 1],
		[12, "log_draw_stack_heavy", 1]
	])("draw stack total %i uses %s", (total, key, tier) => {
		const event: LogEvent = { kind: "draw_stack", player: "Ann", victim: "Bob", amount: 4, total };
		expect(buildLogLine(event, streak({ stackedDebt: total }))).toMatchObject({ key, tier });
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

describe("buildLogLine variant selection", () => {
	it("picks the highest matching minRun regardless of array order", () => {
		const unsorted = {
			run: (s: StreakInfo) => s.skipRun,
			variants: [
				{ minRun: 3, key: "third" },
				{ minRun: 1, key: "first" },
				{ minRun: 2, key: "second" }
			],
			params: () => ({})
		};
		const original = LOG_RULES.skip;
		Object.assign(LOG_RULES, { skip: unsorted });
		try {
			const line = buildLogLine(
				{ kind: "skip", player: "Ann" },
				{ skipRun: 2, reverseRun: 0, stackedDebt: 0 }
			);
			expect(line).toMatchObject({ key: "second", tier: 1 });
		} finally {
			Object.assign(LOG_RULES, { skip: original });
		}
	});
});
