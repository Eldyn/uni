import { describe, expect, it } from "vitest";
import { createPlayLogEmitter } from "../../chat/playLog/playLogEmitter";
import { replayTailToLog, type TailEnvelope } from "../../chat/playLog/tailReplay";
import { NO_STREAKS, readStreaks } from "../../chat/playLog/serverStreaks";
import type { LogLine } from "../../chat/playLog/logEvent";

const flush = () => new Promise((resolve) => setTimeout(resolve, 0));
const keys = (lines: LogLine[]) => lines.map((line) => line.key);

function skipEntry(sequence: number, target: string, run: number): TailEnvelope {
	return {
		sequence,
		id: "turn_skipped",
		replayed: true,
		payload: { target },
		context: { "uni:skip_run": run }
	};
}

function reverseEntry(sequence: number, run: number): TailEnvelope {
	return {
		sequence,
		id: "direction_changed",
		replayed: true,
		payload: { direction: -1 },
		context: { "uni:reverse_run": run }
	};
}

describe("readStreaks", () => {
	it("reads no streak info as zero values", () => {
		expect(readStreaks(undefined)).toBe(NO_STREAKS);
		expect(readStreaks({})).toEqual(NO_STREAKS);
		expect(NO_STREAKS.skipRun + NO_STREAKS.drawRun + NO_STREAKS.totalDraws).toBe(0);
		expect(NO_STREAKS.lastWildColor).toBeNull();
	});

	it("reads every server value from the context", () => {
		const values = readStreaks(
			{
				"uni:skip_run": 3,
				"uni:reverse_run": 2,
				"uni:draw_run": { Ann: 4, Bob: 1 },
				"uni:total_draws": 9,
				"uni:wild_run": 2,
				"uni:last_wild_color": { last: "blue", previous: "red" }
			},
			"Ann"
		);
		expect(values).toEqual({
			skipRun: 3,
			reverseRun: 2,
			drawRun: 4,
			totalDraws: 9,
			wildRun: 2,
			lastWildColor: "blue",
			prevWildColor: "red"
		});
	});

	it("reads the draw run of the named seat only", () => {
		const context = { "uni:draw_run": { Ann: 4, Bob: 1 } };
		expect(readStreaks(context, "Bob").drawRun).toBe(1);
		expect(readStreaks(context, "Cy").drawRun).toBe(0);
		expect(readStreaks(context).drawRun).toBe(0);
	});

	it("ignores a malformed value", () => {
		expect(readStreaks({ "uni:skip_run": "3" }).skipRun).toBe(0);
		expect(readStreaks({ "uni:skip_run": -1 }).skipRun).toBe(0);
		expect(readStreaks({ "uni:skip_run": 1.5 }).skipRun).toBe(0);
		expect(readStreaks({ "uni:draw_run": 5 }, "Ann").drawRun).toBe(0);
		expect(readStreaks({ "uni:draw_run": [1] }, "Ann").drawRun).toBe(0);
		expect(readStreaks({ "uni:last_wild_color": "blue" }).lastWildColor).toBeNull();
		expect(readStreaks({ "uni:last_wild_color": { last: 3, previous: "" } })).toMatchObject({
			lastWildColor: null,
			prevWildColor: null
		});
	});
});

describe("play log with the server context", () => {
	it("uses the server skip run in place of the local count", async () => {
		const lines: LogLine[] = [];
		const emitter = createPlayLogEmitter((line) => lines.push(line));
		emitter.noteSkipped({ target: "Bob", seq: 1, context: { "uni:skip_run": 3 } });
		await flush();
		expect(keys(lines)).toEqual(["uni:log.skip_third"]);
	});

	it("uses the server reverse run in place of the local count", async () => {
		const lines: LogLine[] = [];
		const emitter = createPlayLogEmitter((line) => lines.push(line));
		emitter.noteDirection({ direction: -1, seq: 1, context: { "uni:reverse_run": 3 } });
		await flush();
		expect(keys(lines)).toEqual(["uni:log.reverse_again"]);
	});

	it("counts nothing itself: a missing context reads as a first skip", async () => {
		const lines: LogLine[] = [];
		const emitter = createPlayLogEmitter((line) => lines.push(line));
		emitter.noteSkipped({ target: "Bob", seq: 1 });
		emitter.noteSkipped({ target: "Bob", seq: 2 });
		emitter.noteSkipped({ target: "Bob", seq: 3 });
		await flush();
		expect(keys(lines)).toEqual(["uni:log.skip", "uni:log.skip", "uni:log.skip"]);
	});

	it("uses the server draw counts for the drawing seat", async () => {
		const lines: LogLine[] = [];
		const emitter = createPlayLogEmitter((line) => lines.push(line));
		emitter.noteDraw(
			{
				seq: 1,
				player: "Ann",
				count: 1,
				context: { "uni:draw_run": { Ann: 4, Bob: 1 }, "uni:total_draws": 5 }
			},
			{ handSize: 3 }
		);
		emitter.noteDraw(
			{
				seq: 2,
				player: "Bob",
				count: 1,
				context: { "uni:draw_run": { Ann: 4, Bob: 1 }, "uni:total_draws": 1 }
			},
			{ handSize: 3 }
		);
		await flush();
		expect(keys(lines)[0]).toMatch(/^uni:log\.draw_many/);
		expect(keys(lines)[1]).toMatch(/^uni:log\.draw_first/);
	});

	it("uses the server wild run and colors", async () => {
		const lines: LogLine[] = [];
		const emitter = createPlayLogEmitter((line) => lines.push(line));
		emitter.notePlay({ player: "Ann", kind: "uni:wild", color: "white" }, Promise.resolve(), {
			seq: 1
		});
		emitter.noteWildColor("blue", {
			"uni:wild_run": 3,
			"uni:last_wild_color": { last: "blue", previous: "red" }
		});
		await flush();
		const wild = lines.find((line) => line.key.startsWith("uni:log.wild_reaction"));
		expect(wild?.key).toMatch(/^uni:log\.wild_reaction_third/);
		expect(wild?.params.prevColor).toBe("red");
	});
});

describe("late join parity of the skip and reverse runs", () => {
	it("gives a late joiner the lines of a continuous player", async () => {
		const sequence: TailEnvelope[] = [
			skipEntry(0, "Bob", 1),
			skipEntry(1, "Bob", 2),
			reverseEntry(2, 1),
			skipEntry(3, "Cy", 1)
		];

		const continuous: LogLine[] = [];
		const emitter = createPlayLogEmitter((line) => continuous.push(line));
		for (const entry of sequence) {
			if (entry.id === "turn_skipped") {
				emitter.noteSkipped({
					target: String(entry.payload.target),
					seq: entry.sequence,
					context: entry.context
				});
			} else {
				emitter.noteDirection({ direction: -1, seq: entry.sequence, context: entry.context });
			}
		}
		await flush();

		const lateJoiner: LogLine[] = [];
		replayTailToLog(
			sequence,
			() => undefined,
			(line) => lateJoiner.push(line)
		);

		expect(keys(lateJoiner)).toHaveLength(4);
		expect(keys(lateJoiner).map((key) => key.replace(/_2$/, ""))).toEqual(
			keys(continuous).map((key) => key.replace(/_2$/, ""))
		);
	});

	it("shows the server run of a tail that starts in the middle of a streak", () => {
		const lines: LogLine[] = [];
		replayTailToLog(
			[skipEntry(0, "Bob", 3)],
			() => undefined,
			(line) => lines.push(line)
		);
		expect(keys(lines)).toEqual(["uni:log.skip_third"]);
	});
});
