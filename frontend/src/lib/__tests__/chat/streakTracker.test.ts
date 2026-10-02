import { describe, expect, it } from "vitest";
import { StreakTracker } from "../../chat/playLog/streakTracker";

describe("StreakTracker", () => {
	it("counts consecutive skips on the same player", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "skip", player: "Ann" });
		expect(tracker.record({ kind: "skip", player: "Ann" }).skipRun).toBe(2);
	});

	it("restarts the skip run on a different player", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "skip", player: "Ann" });
		tracker.record({ kind: "skip", player: "Ann" });
		expect(tracker.record({ kind: "skip", player: "Bob" }).skipRun).toBe(1);
	});

	it("clears every run on a normal play", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "skip", player: "Ann" });
		tracker.record({ kind: "reverse" });
		const info = tracker.record({
			kind: "play",
			seq: 1,
			player: "Ann",
			cardKind: "vanilla:red_3",
			color: "red"
		});
		expect(info).toEqual({
			skipRun: 0,
			reverseRun: 0,
			stackedDebt: 0,
			drawRun: 0,
			totalDraws: 0
		});
	});

	it("keeps a same-player skip run through the skip card's own play", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "skip", player: "Ann" });
		tracker.record({
			kind: "play",
			seq: 1,
			player: "Bob",
			cardKind: "vanilla:red_skip",
			color: "red"
		});
		expect(tracker.record({ kind: "skip", player: "Ann" }).skipRun).toBe(2);
	});

	it("keeps a reverse run through the reverse card's own play", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "reverse" });
		tracker.record({
			kind: "play",
			seq: 1,
			player: "Ann",
			cardKind: "vanilla:red_reverse",
			color: "red"
		});
		expect(tracker.record({ kind: "reverse" }).reverseRun).toBe(2);
	});

	it("counts reverses in a row", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "reverse" });
		tracker.record({ kind: "reverse" });
		expect(tracker.record({ kind: "reverse" }).reverseRun).toBe(3);
	});

	it("counts per-player draw runs and the match draw total", () => {
		const tracker = new StreakTracker();
		const draw = (player: string, seq: number) =>
			tracker.record({ kind: "draw", seq, player, count: 1, penalty: false });
		expect(draw("Ann", 1)).toMatchObject({ drawRun: 1, totalDraws: 1 });
		expect(draw("Bob", 2)).toMatchObject({ drawRun: 1, totalDraws: 2 });
		expect(draw("Ann", 3)).toMatchObject({ drawRun: 2, totalDraws: 3 });
	});

	it("clears the draw counters on reset", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "draw", seq: 1, player: "Ann", count: 1, penalty: false });
		tracker.record({ kind: "draw", seq: 2, player: "Ann", count: 1, penalty: false });
		tracker.reset();
		expect(
			tracker.record({ kind: "draw", seq: 3, player: "Ann", count: 1, penalty: false })
		).toMatchObject({ drawRun: 1, totalDraws: 1 });
	});

	it("reset clears the state", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "skip", player: "Ann" });
		tracker.record({ kind: "reverse" });
		tracker.reset();
		expect(tracker.record({ kind: "skip", player: "Ann" })).toEqual({
			skipRun: 1,
			reverseRun: 0,
			stackedDebt: 0,
			drawRun: 0,
			totalDraws: 0
		});
	});

	it("a fresh tracker starts the first skip at run 1", () => {
		expect(new StreakTracker().record({ kind: "skip", player: "Ann" }).skipRun).toBe(1);
	});
});
