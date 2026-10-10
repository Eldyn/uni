import { describe, expect, it } from "vitest";
import { createPlayLogEmitter } from "../../chat/playLog/playLogEmitter";
import type { LogLine } from "../../chat/playLog/logEvent";

const STREAK_POOL = ["uni:log.skip_streak", "uni:log.skip_streak_2"];
const WILD_SECOND = ["uni:log.wild_reaction_second_1", "uni:log.wild_reaction_second_2"];
const WILD_THIRD = ["uni:log.wild_reaction_third_1", "uni:log.wild_reaction_third_2"];

function client() {
	const lines: LogLine[] = [];
	return { lines, emitter: createPlayLogEmitter((line) => lines.push(line)) };
}

const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

function drive(emitter: ReturnType<typeof client>["emitter"], seqs: number[]) {
	seqs.forEach((seq, index) => {
		emitter.noteSkipped({ target: "Ann", seq, context: { "uni:skip_run": index + 1 } });
	});
}

describe("two clients telling the same joke", () => {
	it("produces identical keys for the same beat stream", async () => {
		const a = client();
		const b = client();
		const stream = [1, 1, 1, 1, 2];
		drive(a.emitter, stream);
		drive(b.emitter, stream);
		await flush();
		expect(a.lines.map((line) => line.key)).toEqual(b.lines.map((line) => line.key));
	});

	it("rotates deterministically within a multi-key pool", async () => {
		const a = client();
		const b = client();
		const stream = [1, 1, 1, 1, 2];
		drive(a.emitter, stream);
		drive(b.emitter, stream);
		await flush();

		const keys = a.lines.map((line) => line.key);
		expect(keys).toEqual(b.lines.map((line) => line.key));
		expect(keys.slice(0, 3)).toEqual([
			"uni:log.skip",
			"uni:log.skip_again_2",
			"uni:log.skip_third"
		]);

		const [firstStreak, secondStreak] = keys.slice(3);
		expect(STREAK_POOL).toContain(firstStreak);
		expect(STREAK_POOL).toContain(secondStreak);
		expect(firstStreak).not.toEqual(secondStreak);
	});

	it("keeps two clients in step through the wild second and third chains", async () => {
		const a = client();
		const b = client();
		const wilds = [
			{ color: "red", seq: 1 },
			{ color: "blue", seq: 2 },
			{ color: "green", seq: 3 }
		];
		const driveWilds = (emitter: ReturnType<typeof client>["emitter"]) => {
			wilds.forEach(({ color, seq }, index) => {
				emitter.notePlay({ player: "Ann", kind: "uni:wild", color: "white" }, Promise.resolve(), {
					seq
				});
				emitter.noteWildColor(color, { "uni:wild_run": index + 1 });
			});
		};
		driveWilds(a.emitter);
		driveWilds(b.emitter);
		await flush();

		const keys = a.lines.map((line) => line.key);
		expect(keys).toEqual(b.lines.map((line) => line.key));

		const second = keys.filter((key) => key.startsWith("uni:log.wild_reaction_second"));
		const third = keys.filter((key) => key.startsWith("uni:log.wild_reaction_third"));
		expect(second).toHaveLength(1);
		expect(third).toHaveLength(1);
		expect(WILD_SECOND).toContain(second[0]);
		expect(WILD_THIRD).toContain(third[0]);
	});
});
