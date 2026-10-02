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
			drawRun: 0,
			totalDraws: 0,
			wildRun: 0,
			lastWildColor: null,
			prevWildColor: null,
			nearWinTarget: null,
			nearWinOpen: false
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

	it("resets the skip run when a draw breaks two same-player skips (Review Focus)", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "skip", player: "Ann" });
		tracker.record({ kind: "draw", seq: 1, player: "Bob", count: 1, penalty: false });
		expect(tracker.record({ kind: "skip", player: "Ann" }).skipRun).toBe(1);
	});

	it("resets the reverse run on a draw", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "reverse" });
		tracker.record({ kind: "draw", seq: 1, player: "Bob", count: 1, penalty: false });
		expect(tracker.record({ kind: "reverse" }).reverseRun).toBe(1);
	});

	it("reset clears the state", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "skip", player: "Ann" });
		tracker.record({ kind: "reverse" });
		tracker.reset();
		expect(tracker.record({ kind: "skip", player: "Ann" })).toEqual({
			skipRun: 1,
			reverseRun: 0,
			drawRun: 0,
			totalDraws: 0,
			wildRun: 0,
			lastWildColor: null,
			prevWildColor: null,
			nearWinTarget: null,
			nearWinOpen: false
		});
	});

	it("a fresh tracker starts the first skip at run 1", () => {
		expect(new StreakTracker().record({ kind: "skip", player: "Ann" }).skipRun).toBe(1);
	});

	it("counts consecutive wild picks and remembers the previous colour", () => {
		const t = new StreakTracker();
		t.record({ kind: "wild", seq: 1, player: "Ann", color: "red" });
		const second = t.record({ kind: "wild", seq: 2, player: "Bob", color: "blue" });
		expect(second.wildRun).toBe(2);
		expect(second.lastWildColor).toBe("blue");
		expect(second.prevWildColor).toBe("red");
	});

	it("does not reset a wild run on the wild card's own play", () => {
		const t = new StreakTracker();
		t.record({ kind: "wild", seq: 1, player: "Ann", color: "red" });
		const afterPlay = t.record({
			kind: "play",
			seq: 2,
			player: "Ann",
			cardKind: "vanilla:wild",
			color: "red"
		});
		expect(afterPlay.wildRun).toBe(1);
	});

	it("resets the wild run on a non-wild play", () => {
		const t = new StreakTracker();
		t.record({ kind: "wild", seq: 1, player: "Ann", color: "red" });
		expect(
			t.record({ kind: "play", seq: 2, player: "Ann", cardKind: "vanilla:red_5", color: "red" })
				.wildRun
		).toBe(0);
	});

	it("resets the wild run on a skip", () => {
		const t = new StreakTracker();
		t.record({ kind: "wild", seq: 1, player: "Ann", color: "red" });
		expect(t.record({ kind: "skip", seq: 2, player: "Bob" }).wildRun).toBe(0);
	});

	it("resets the wild run on a reverse", () => {
		const t = new StreakTracker();
		t.record({ kind: "wild", seq: 1, player: "Ann", color: "red" });
		expect(t.record({ kind: "reverse", seq: 2 }).wildRun).toBe(0);
	});

	it("opens a near-win window and reports it entering a skip on the target", () => {
		const t = new StreakTracker();
		t.record({ kind: "near_win", seq: 1, player: "Bob" });
		const skip = t.record({ kind: "skip", seq: 2, player: "Bob" });
		expect(skip.nearWinOpen).toBe(true);
		expect(skip.nearWinTarget).toBe("Bob");
	});

	it("closes the near-win window after a block", () => {
		const t = new StreakTracker();
		t.record({ kind: "near_win", seq: 1, player: "Bob" });
		t.record({ kind: "skip", seq: 2, player: "Bob" });
		expect(t.record({ kind: "skip", seq: 3, player: "Ann" }).nearWinOpen).toBe(false);
	});

	it("closes the near-win window when the target plays", () => {
		const t = new StreakTracker();
		t.record({ kind: "near_win", seq: 1, player: "Bob" });
		expect(
			t.record({ kind: "play", seq: 2, player: "Bob", cardKind: "vanilla:red_5", color: "red" })
				.nearWinOpen
		).toBe(false);
	});

	it("closes the near-win window on a penalty draw aimed at the target", () => {
		const t = new StreakTracker();
		t.record({ kind: "near_win", seq: 1, player: "Bob" });
		const block = t.record({
			kind: "draw",
			seq: 2,
			player: "Ann",
			count: 2,
			penalty: true,
			victim: "Bob"
		});
		expect(block.nearWinOpen).toBe(true);
		expect(block.nearWinTarget).toBe("Bob");
		const after = t.record({
			kind: "play",
			seq: 3,
			player: "Cara",
			cardKind: "vanilla:red_5",
			color: "red"
		});
		expect(after.nearWinOpen).toBe(false);
	});

	it("closes the near-win window when the target draws without a penalty", () => {
		const t = new StreakTracker();
		t.record({ kind: "near_win", seq: 1, player: "Bob" });
		const draw = t.record({ kind: "draw", seq: 2, player: "Bob", count: 1 });
		expect(draw.nearWinOpen).toBe(false);
		expect(draw.nearWinTarget).toBeNull();
	});

	it("keeps the near-win window open through a skip aimed at someone else", () => {
		const t = new StreakTracker();
		t.record({ kind: "near_win", seq: 1, player: "Bob" });
		const skip = t.record({ kind: "skip", seq: 2, player: "Cara" });
		expect(skip.nearWinOpen).toBe(true);
		expect(skip.nearWinTarget).toBe("Bob");
	});
});
