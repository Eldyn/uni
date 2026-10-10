import { describe, expect, it, beforeEach, vi } from "vitest";
import { createPlayLogEmitter, DRAW_DEBT_STATUS_KIND } from "../../chat/playLog/playLogEmitter";
import type { LogLine } from "../../chat/playLog/logEvent";

const { escaped } = vi.hoisted(() => ({ escaped: [] as string[] }));

vi.mock(import("../../chat/playLog/logRichText"), async (importOriginal) => {
	const actual = await importOriginal();
	return {
		...actual,
		escapeLogText: (value: string) => {
			escaped.push(value);
			return actual.escapeLogText(value);
		}
	};
});

const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

function setup() {
	const lines: LogLine[] = [];
	const emitter = createPlayLogEmitter((line) => lines.push(line));
	return { lines, emitter };
}

const skipRun = (run: number) => ({ "uni:skip_run": run });
const reverseRun = (run: number) => ({ "uni:reverse_run": run });

describe("createPlayLogEmitter", () => {
	let lines: LogLine[];
	let emitter: ReturnType<typeof createPlayLogEmitter>;

	beforeEach(() => {
		({ lines, emitter } = setup());
		escaped.length = 0;
	});

	it("logs a skip per skipped seat, escalating with the server run", async () => {
		emitter.noteSkipped({ target: "Ann", seq: 1, context: skipRun(1) });
		emitter.noteSkipped({ target: "Ann", seq: 1, context: skipRun(2) });
		emitter.noteSkipped({ target: "Ann", seq: 1, context: skipRun(3) });
		await flush();

		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.skip",
			"uni:log.skip_again_2",
			"uni:log.skip_third"
		]);
	});

	it("threads the turn seq so a streak pool actually rotates (Review Focus)", async () => {
		emitter.noteSkipped({ target: "Ann", seq: 1, context: skipRun(1) });
		emitter.noteSkipped({ target: "Ann", seq: 1, context: skipRun(2) });
		emitter.noteSkipped({ target: "Ann", seq: 1, context: skipRun(3) });
		emitter.noteSkipped({ target: "Ann", seq: 1, context: skipRun(4) });
		emitter.noteSkipped({ target: "Ann", seq: 2, context: skipRun(5) });
		await flush();

		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.skip",
			"uni:log.skip_again_2",
			"uni:log.skip_third",
			"uni:log.skip_streak_2",
			"uni:log.skip_streak"
		]);
	});

	it("logs one line per skipped seat, in order", async () => {
		emitter.noteSkipped({ target: "Ann", seq: 1 });
		emitter.noteSkipped({ target: "Bob", seq: 2 });
		await flush();
		expect(lines.map((line) => line.params.name)).toEqual(["Ann", "Bob"]);
	});

	it("logs a reverse when the direction flips, escalating with the server run", async () => {
		emitter.noteDirection({ direction: -1, seq: 1, context: reverseRun(1) });
		emitter.noteDirection({ direction: 1, seq: 1, context: reverseRun(2) });
		emitter.noteDirection({ direction: -1, seq: 1, context: reverseRun(3) });
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.reverse",
			expect.stringMatching(/^uni:log\.reverse_back/),
			"uni:log.reverse_again"
		]);
	});

	it("logs a reverse from a direction event, even for the first direction seen", async () => {
		emitter.noteDirection({ direction: -1, seq: 1 });
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["uni:log.reverse"]);
	});

	it("logs two direction events in a row", async () => {
		emitter.noteDirection({ direction: -1, seq: 1, context: reverseRun(1) });
		emitter.noteDirection({ direction: 1, seq: 2, context: reverseRun(2) });
		await flush();
		expect(lines).toHaveLength(2);
		expect(lines[0]!.key).toBe("uni:log.reverse");
		expect(lines[1]!.key.startsWith("uni:log.reverse_back")).toBe(true);
	});

	it("holds a play's lines until its card has landed", async () => {
		let land!: () => void;
		const landed = new Promise<void>((resolve) => (land = resolve));
		emitter.notePlay({ player: "Ann", kind: "uni:blue_5", color: "blue" }, landed, { seq: 1 });
		emitter.noteSkipped({ target: "Bob", seq: 2 });
		await flush();
		expect(lines).toEqual([]);

		land();
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["uni:log.play", "uni:log.skip"]);
	});

	it("a plain play between skips breaks the skip run", async () => {
		emitter.noteSkipped({ target: "Ann", seq: 1 });
		emitter.notePlay({ player: "Bob", kind: "uni:red_3", color: "red" }, Promise.resolve(), {
			seq: 2
		});
		emitter.noteSkipped({ target: "Ann", seq: 3 });
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["uni:log.skip", "uni:log.play", "uni:log.skip"]);
	});

	it("logs every play, including specials", async () => {
		emitter.notePlay({ player: "Ann", kind: "uni:red_skip", color: "red" }, Promise.resolve(), {
			seq: 1
		});
		emitter.notePlay(
			{ player: "Bob", kind: "uni:green_draw2", color: "green" },
			Promise.resolve(),
			{ seq: 2 }
		);
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["uni:log.play", "uni:log.play"]);
		expect(lines[1].params).toEqual({
			name: "Bob",
			kind: "uni:green_draw2",
			color: "green"
		});
	});

	it("logs a forced play as an auto play, not a play", async () => {
		emitter.notePlay(
			{ player: "Ann", kind: "uni:red_skip", color: "red", auto: true },
			Promise.resolve(),
			{
				seq: 3
			}
		);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([expect.stringMatching(/^uni:log\.auto_play/)]);
		expect(lines[0].params).toEqual({ name: "Ann", kind: "uni:red_skip", color: "red" });
	});

	it("logs a wild colour reaction alongside the play", async () => {
		emitter.notePlay({ player: "Ann", kind: "uni:wild", color: "red" }, Promise.resolve(), {
			seq: 4
		});
		await flush();
		expect(lines[0].key).toBe("uni:log.play");
		expect(lines[1].key).toMatch(/^uni:log\.wild_reaction_(1|first_2)$/);
		expect(lines[1].params).toEqual({ name: "Ann", color: "red", prevColor: "" });
	});

	it("logs a wild colour reaction for a uni wild", async () => {
		emitter.notePlay({ player: "Ann", kind: "uni:wild", color: "red" }, Promise.resolve(), {
			seq: 4
		});
		await flush();
		expect(lines[1].key).toMatch(/^uni:log\.wild_reaction_(1|first_2)$/);
	});

	it("exposes the uni draw debt status kind", () => {
		expect(DRAW_DEBT_STATUS_KIND).toBe("uni:draw_debt");
	});

	it("logs the wild colour reaction once the colour arrives", async () => {
		let land!: () => void;
		const landed = new Promise<void>((resolve) => (land = resolve));
		emitter.notePlay({ player: "Ann", kind: "uni:wild", color: "white" }, landed, { seq: 5 });
		emitter.noteWildColor("blue");
		land();
		await flush();
		expect(lines[0].key).toBe("uni:log.play");
		expect(lines[1].key).toMatch(/^uni:log\.wild_reaction_(1|first_2)$/);
		expect(lines[1].params).toEqual({ name: "Ann", color: "blue", prevColor: "" });
	});

	it("keeps a forced wild as an auto play once its colour arrives", async () => {
		let land!: () => void;
		const landed = new Promise<void>((resolve) => (land = resolve));
		emitter.notePlay({ player: "Ann", kind: "uni:wild", color: "white", auto: true }, landed, {
			seq: 6
		});
		emitter.noteWildColor("blue");
		land();
		await flush();
		expect(lines[0].key).toMatch(/^uni:log\.auto_play/);
		expect(lines[1].key).toMatch(/^uni:log\.wild_reaction_(1|first_2)$/);
	});

	it("logs a near-win reaction from the near-win event after the play", async () => {
		emitter.notePlay({ player: "Ann", kind: "uni:red_5", color: "red" }, Promise.resolve(), {
			seq: 6
		});
		emitter.noteNearWin("Ann", 7);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.play",
			expect.stringMatching(/^uni:log\.near_win/)
		]);
		expect(lines[1].params).toEqual({ name: "Ann" });
	});

	it("logs a reshuffle", async () => {
		emitter.noteReshuffle({ seq: 7 });
		await flush();
		expect(lines.map((line) => line.key)).toEqual([expect.stringMatching(/^uni:log\.reshuffle/)]);
	});

	it("logs a keep without leaking the kept card (Review Focus)", async () => {
		emitter.noteKeep("Ann", 9);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([expect.stringMatching(/^uni:log\.keep_drawn/)]);
		expect(lines[0].params).toEqual({ name: "Ann" });
	});

	it("logs two keeps by one seat", async () => {
		emitter.noteKeep("Ann", 9);
		emitter.noteKeep("Ann", 20);
		await flush();
		expect(lines).toHaveLength(2);
	});

	it("logs the draw line and the penalty line of one debt draw", async () => {
		emitter.notePlay(
			{ player: "Ann", kind: "uni:green_draw2", color: "green" },
			Promise.resolve(),
			{ seq: 5 }
		);
		emitter.noteDebt("Bob", 2);
		emitter.noteDraw({ seq: 6, player: "Bob", count: 2 }, { handSize: 5 });
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.play",
			"uni:log.draw_stack",
			expect.stringMatching(/^uni:log\.draw_/)
		]);
	});

	it("logs the penalty line from the debt status with no card moved", async () => {
		emitter.notePlay(
			{ player: "Ann", kind: "uni:green_draw2", color: "green" },
			Promise.resolve(),
			{ seq: 5 }
		);
		emitter.noteDebt("Bob", 2);
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["uni:log.play", "uni:log.draw_stack"]);
	});

	it("logs a win and each later placement once", async () => {
		emitter.noteMatchEnded("Ann", 2);
		emitter.notePlacement("Bob", 2, false);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			expect.stringMatching(/^uni:log\.win/),
			expect.stringMatching(/^uni:log\.left_behind/)
		]);
		expect(lines[1].params).toEqual({ name: "Bob", place: 2 });

		emitter.noteMatchEnded("Ann", 2);
		emitter.notePlacement("Bob", 2, false);
		await flush();
		expect(lines).toHaveLength(2);
	});

	it("words places up to the finisher count as finishing and the rest as left behind", async () => {
		emitter.noteMatchEnded("Ann", 3);
		emitter.notePlacement("Bob", 2, false, 2);
		emitter.notePlacement("Cy", 3, false, 2);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			expect.stringMatching(/^uni:log\.win/),
			expect.stringMatching(/^uni:log\.finish/),
			expect.stringMatching(/^uni:log\.left_behind/)
		]);
		expect(lines[2].params).toEqual({ name: "Cy", place: 3 });
	});

	it("takes no line for a first place; the match end line is the win line", async () => {
		emitter.notePlacement("Ann", 1, false);
		emitter.noteMatchEnded("Ann", 1);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([expect.stringMatching(/^uni:log\.win/)]);
	});

	it("words an elimination place as left behind even inside the finisher count", async () => {
		emitter.notePlacement("Bob", 2, true, 3);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([expect.stringMatching(/^uni:log\.left_behind/)]);
	});

	it("reset forgets the placement ledger", async () => {
		emitter.notePlacement("Bob", 2, false);
		await flush();
		emitter.reset();
		emitter.notePlacement("Bob", 2, false);
		await flush();
		expect(lines).toHaveLength(2);
	});

	it("reset clears the game-end trackers", async () => {
		emitter.noteMatchEnded("Ann", 2);
		emitter.notePlacement("Bob", 2, false);
		await flush();
		emitter.reset();
		emitter.noteMatchEnded("Ann", 2);
		emitter.notePlacement("Bob", 2, false);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			expect.stringMatching(/^uni:log\.win/),
			expect.stringMatching(/^uni:log\.left_behind/),
			expect.stringMatching(/^uni:log\.win/),
			expect.stringMatching(/^uni:log\.left_behind/)
		]);
	});

	it("logs a wild once its colour is already known", async () => {
		emitter.notePlay({ player: "Ann", kind: "uni:wild", color: "red" }, Promise.resolve(), {
			seq: 4
		});
		await flush();
		expect(lines[0].key).toBe("uni:log.play");
		expect(lines[1].key).toMatch(/^uni:log\.wild_reaction_(1|first_2)$/);
		expect(lines[0].params).toEqual({ name: "Ann", kind: "uni:wild", color: "red" });
	});

	it("logs a play for a wild once its colour arrives", async () => {
		let land!: () => void;
		const landed = new Promise<void>((resolve) => (land = resolve));
		emitter.notePlay({ player: "Ann", kind: "uni:wild", color: "white" }, landed, { seq: 3 });
		await flush();
		expect(lines).toEqual([]);

		emitter.noteWildColor("red");
		land();
		await flush();
		expect(lines.map((line) => line.params)).toEqual([
			{ name: "Ann", kind: "uni:wild", color: "red" },
			{ name: "Ann", color: "red", prevColor: "" }
		]);
	});

	it("logs a draw stack from a +N play followed by the debt status", async () => {
		emitter.notePlay(
			{ player: "Ann", kind: "uni:green_draw2", color: "green" },
			Promise.resolve(),
			{ seq: 5 }
		);
		emitter.noteDebt("Bob", 6);
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["uni:log.play", "uni:log.draw_stack"]);
		expect(lines[1].params).toMatchObject({ name: "Ann", victim: "Bob", amount: 2, total: 6 });
	});

	it("reads the debt magnitude as the running total across a stacked +2 then +4", async () => {
		// INFO: the engine applies magnitude + carried debt, so the status_applied
		// magnitude is already the running total on the new victim.
		emitter.notePlay(
			{ player: "Ann", kind: "uni:green_draw2", color: "green" },
			Promise.resolve(),
			{ seq: 5 }
		);
		emitter.noteDebt("Bob", 2);
		emitter.notePlay({ player: "Bob", kind: "uni:wild_draw4", color: "red" }, Promise.resolve(), {
			seq: 6
		});
		emitter.noteDebt("Cy", 6);
		await flush();

		const stacks = lines.filter((line) => line.key === "uni:log.draw_stack");
		expect(stacks.map((line) => line.params)).toMatchObject([
			{ name: "Ann", victim: "Bob", amount: 2, total: 2 },
			{ name: "Bob", victim: "Cy", amount: 4, total: 6 }
		]);
	});

	it("ignores a debt status with no preceding +N play", async () => {
		emitter.noteDebt("Bob", 2);
		await flush();
		expect(lines).toEqual([]);
	});

	it("escapes hostile names so they cannot inject markup (Review Focus 1)", async () => {
		emitter.noteSkipped({ target: "[fx=shake]x[/fx]", seq: 4 });
		emitter.notePlay(
			{ player: "[c=red]Ann", kind: "uni:green_draw2", color: "green" },
			Promise.resolve(),
			{ seq: 5 }
		);
		emitter.noteDebt("[b]Bob", 4);
		await flush();

		for (const line of lines) {
			for (const value of Object.values(line.params)) {
				if (typeof value !== "string") continue;
				expect(value).not.toMatch(/(^|[^\\])\[/);
			}
		}
		expect(lines).toHaveLength(3);
	});

	it("a reset drops the queued lines; the next skip reads its own server run", async () => {
		emitter.noteSkipped({ target: "Ann", seq: 1, context: skipRun(1) });
		emitter.noteSkipped({ target: "Ann", seq: 1, context: skipRun(2) });
		await flush();
		emitter.reset();
		emitter.noteSkipped({ target: "Ann", seq: 1, context: skipRun(1) });
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.skip",
			"uni:log.skip_again_2",
			"uni:log.skip"
		]);
	});

	it("a reset forgets the direction baseline and drops queued lines", async () => {
		let land!: () => void;
		const landed = new Promise<void>((resolve) => (land = resolve));
		emitter.noteDirection({ direction: -1, seq: 1 });
		emitter.notePlay({ player: "Ann", kind: "uni:red_skip", color: "red" }, landed, { seq: 1 });
		emitter.noteSkipped({ target: "Bob", seq: 2 });
		await flush();
		emitter.reset();
		land();
		await flush();
		const afterReset = lines.length;
		emitter.noteDirection({ direction: -1, seq: 3 });
		await flush();
		expect(lines).toHaveLength(afterReset + 1);
	});

	it("carries the skip actor onto the emitted skip event", async () => {
		emitter.noteSkipped({ target: "Bob", actor: "Ann", seq: 5 });
		await flush();
		const line = lines[0];
		expect(line.key).toBe("uni:log.skip");
		// INFO: the actor is not part of LogLine.params yet, so it is observed at
		//       the escape seam the emitter runs it through.
		expect(escaped).toContain("Ann");
	});

	it("logs a blocked retort from the blocked event, after the skip line", async () => {
		emitter.notePlay({ player: "Bob", kind: "uni:red_5", color: "red" }, Promise.resolve(), {
			seq: 1
		});
		emitter.noteNearWin("Bob", 2);
		emitter.noteSkipped({ target: "Bob", actor: "Ann", seq: 3, context: skipRun(1) });
		emitter.noteBlocked("Bob", "Ann", 4);
		await flush();

		const skipIndex = lines.findIndex((line) => line.key === "uni:log.skip");
		const blockedIndex = lines.findIndex((line) => line.key.startsWith("uni:log.blocked_"));
		expect(skipIndex).toBeGreaterThanOrEqual(0);
		expect(blockedIndex).toBeGreaterThan(skipIndex);
		expect(lines[blockedIndex].params).toEqual({ blocker: "Ann", target: "Bob" });
	});

	it("logs a blocked retort for a penalty draw after the stack line", async () => {
		emitter.notePlay({ player: "Ann", kind: "uni:red_draw2", color: "red" }, Promise.resolve(), {
			seq: 2
		});
		emitter.noteDebt("Bob", 2);
		emitter.noteBlocked("Bob", "Ann", 3);
		await flush();

		const drawIndex = lines.findIndex((line) => line.key === "uni:log.draw_stack");
		const blockedIndex = lines.findIndex((line) => line.key.startsWith("uni:log.blocked_"));
		expect(drawIndex).toBeGreaterThanOrEqual(0);
		expect(blockedIndex).toBeGreaterThan(drawIndex);
		expect(lines[blockedIndex].params).toEqual({ blocker: "Ann", target: "Bob" });
	});

	it("logs no blocked line when the event names no blocker", async () => {
		emitter.noteBlocked("Bob", undefined, 4);
		await flush();
		expect(lines).toEqual([]);
	});

	it("derives no blocked line from a skip: only the blocked event logs it", async () => {
		emitter.notePlay({ player: "Bob", kind: "uni:red_5", color: "red" }, Promise.resolve(), {
			seq: 1
		});
		emitter.noteSkipped({ target: "Bob", actor: "Cy", seq: 3, context: skipRun(1) });
		await flush();

		expect(lines.filter((line) => line.key === "uni:log.skip")).toHaveLength(1);
		expect(lines.some((line) => line.key.startsWith("uni:log.blocked_"))).toBe(false);
		expect(lines.some((line) => line.key.startsWith("uni:log.near_win"))).toBe(false);
	});

	it("holds the near-win line of a wild until its play line", async () => {
		emitter.notePlay({ player: "Bob", kind: "uni:wild", color: "white" }, Promise.resolve(), {
			seq: 1
		});
		emitter.noteNearWin("Bob", 2);
		emitter.noteWildColor("blue", { "uni:wild_run": 1 });
		await flush();

		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.play",
			expect.stringMatching(/^uni:log\.near_win/),
			expect.stringMatching(/^uni:log\.wild_reaction/)
		]);
	});

	describe("the log queue is separate from the animation queue", () => {
		const hangingLanding = () => new Promise<void>(() => undefined);
		const line = (key: string): LogLine => ({ kind: "log", key, params: {}, tier: 0 });

		it("posts every held line once, in order, when the gates are released", async () => {
			emitter.notePlay({ player: "Ann", kind: "uni:blue_5", color: "blue" }, hangingLanding(), {
				seq: 1
			});
			emitter.noteSkipped({ target: "Bob", seq: 2 });
			emitter.noteDraw({ seq: 3, player: "Bob", count: 1 }, { handSize: 4 });
			await flush();
			expect(lines).toEqual([]);

			emitter.releaseGates();
			await flush();
			expect(lines.map((entry) => entry.key.replace(/_\d+$/, ""))).toEqual([
				"uni:log.play",
				"uni:log.skip",
				expect.stringMatching(/^uni:log\.draw/)
			]);
			emitter.releaseGates();
			await flush();
			expect(lines).toHaveLength(3);
		});

		it("posts a line of a presentation behind the lines of the earlier events", async () => {
			emitter.notePlay({ player: "Ann", kind: "uni:blue_5", color: "blue" }, hangingLanding(), {
				seq: 1
			});
			emitter.noteLine(line("my_mod_apple"));
			await flush();
			expect(lines).toEqual([]);

			emitter.releaseGates();
			await flush();
			expect(lines.map((entry) => entry.key)).toEqual(["uni:log.play", "my_mod_apple"]);
		});

		it("posts a line of a presentation at once when no gate holds it", async () => {
			emitter.noteLine(line("my_mod_apple"));
			await flush();
			expect(lines.map((entry) => entry.key)).toEqual(["my_mod_apple"]);
		});

		it("keeps the order of the events after a release", async () => {
			emitter.notePlay({ player: "Ann", kind: "uni:blue_5", color: "blue" }, hangingLanding(), {
				seq: 1
			});
			emitter.releaseGates();
			emitter.noteLine(line("after_release"));
			await flush();
			expect(lines.map((entry) => entry.key)).toEqual(["uni:log.play", "after_release"]);
		});

		it("drops the held lines on a reset", async () => {
			emitter.notePlay({ player: "Ann", kind: "uni:blue_5", color: "blue" }, hangingLanding(), {
				seq: 1
			});
			emitter.noteLine(line("my_mod_apple"));
			emitter.reset();
			await flush();
			expect(lines).toEqual([]);
			expect(emitter.pendingLines()).toBe(0);
		});
	});
});
