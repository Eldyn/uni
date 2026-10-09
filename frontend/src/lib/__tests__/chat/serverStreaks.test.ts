import { describe, expect, it } from "vitest";
import { createPlayLogEmitter } from "../../chat/playLog/playLogEmitter";
import { replayTailToLog, type TailEnvelope } from "../../chat/playLog/tailReplay";
import { withServerStreaks } from "../../chat/playLog/serverStreaks";
import type { LogLine, StreakInfo } from "../../chat/playLog/logEvent";

const flush = () => new Promise((resolve) => setTimeout(resolve, 0));
const keys = (lines: LogLine[]) => lines.map((line) => line.key);

const LOCAL: StreakInfo = {
	skipRun: 1,
	reverseRun: 1,
	drawRun: 0,
	totalDraws: 0,
	wildRun: 0,
	lastWildColor: null,
	prevWildColor: null,
	nearWinTarget: null,
	nearWinOpen: false
};

function skipEntry(sequence: number, target: string, run: number): TailEnvelope {
	return {
		sequence,
		seq: sequence,
		id: "turn_skipped",
		type: "turn_skipped",
		replayed: true,
		payload: { target },
		context: { "uni:skip_run": run }
	};
}

function reverseEntry(sequence: number, run: number): TailEnvelope {
	return {
		sequence,
		seq: sequence,
		id: "direction_changed",
		type: "direction_changed",
		replayed: true,
		payload: { direction: -1 },
		context: { "uni:reverse_run": run }
	};
}

describe("withServerStreaks", () => {
	it("keeps the local value when the context is missing", () => {
		expect(withServerStreaks(LOCAL, undefined)).toBe(LOCAL);
		expect(withServerStreaks(LOCAL, {})).toBe(LOCAL);
	});

	it("takes the server run values when the context has them", () => {
		const merged = withServerStreaks(LOCAL, { "uni:skip_run": 3, "uni:reverse_run": 2 });
		expect(merged.skipRun).toBe(3);
		expect(merged.reverseRun).toBe(2);
		expect(merged.drawRun).toBe(LOCAL.drawRun);
	});

	it("ignores a malformed value", () => {
		expect(withServerStreaks(LOCAL, { "uni:skip_run": "3" }).skipRun).toBe(1);
		expect(withServerStreaks(LOCAL, { "uni:skip_run": -1 }).skipRun).toBe(1);
		expect(withServerStreaks(LOCAL, { "uni:skip_run": 1.5 }).skipRun).toBe(1);
	});
});

describe("play log with the server context", () => {
	it("uses the server skip run in place of the local count", async () => {
		const lines: LogLine[] = [];
		const emitter = createPlayLogEmitter((line) => lines.push(line));
		emitter.noteSkipped({ target: "Bob", seq: 1, context: { "uni:skip_run": 3 } });
		await flush();
		expect(keys(lines)).toEqual(["log_skip_third"]);
	});

	it("uses the server reverse run in place of the local count", async () => {
		const lines: LogLine[] = [];
		const emitter = createPlayLogEmitter((line) => lines.push(line));
		emitter.noteDirection({ direction: -1, seq: 1, context: { "uni:reverse_run": 3 } });
		await flush();
		expect(keys(lines)).toEqual(["log_reverse_again"]);
	});

	it("falls back to the local count when the context is missing", async () => {
		const lines: LogLine[] = [];
		const emitter = createPlayLogEmitter((line) => lines.push(line));
		emitter.noteSkipped({ target: "Bob", seq: 1 });
		emitter.noteSkipped({ target: "Bob", seq: 2 });
		await flush();
		expect(keys(lines)[0]).toBe("log_skip");
		expect(["log_skip_again", "log_skip_again_2"]).toContain(keys(lines)[1]);
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
					seq: entry.seq,
					context: entry.context
				});
			} else {
				emitter.noteDirection({ direction: -1, seq: entry.seq, context: entry.context });
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
		expect(keys(lines)).toEqual(["log_skip_third"]);
	});
});
