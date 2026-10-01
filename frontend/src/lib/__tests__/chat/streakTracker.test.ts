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
		const info = tracker.record({ kind: "play", player: "Ann", color: "red", value: "3" });
		expect(info).toEqual({ skipRun: 0, reverseRun: 0, stackedDebt: 0 });
	});

	it("counts reverses in a row", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "reverse" });
		tracker.record({ kind: "reverse" });
		expect(tracker.record({ kind: "reverse" }).reverseRun).toBe(3);
	});

	it("tracks the stacked draw total", () => {
		const tracker = new StreakTracker();
		const event = {
			kind: "draw_stack" as const,
			player: "Ann",
			victim: "Bob",
			amount: 2
		};
		tracker.record({ ...event, total: 2 });
		expect(tracker.record({ ...event, total: 4 }).stackedDebt).toBe(4);
	});

	it("reset clears the state", () => {
		const tracker = new StreakTracker();
		tracker.record({ kind: "skip", player: "Ann" });
		tracker.record({ kind: "reverse" });
		tracker.reset();
		expect(tracker.record({ kind: "skip", player: "Ann" })).toEqual({
			skipRun: 1,
			reverseRun: 0,
			stackedDebt: 0
		});
	});

	it("a fresh tracker starts the first skip at run 1", () => {
		expect(new StreakTracker().record({ kind: "skip", player: "Ann" }).skipRun).toBe(1);
	});
});
