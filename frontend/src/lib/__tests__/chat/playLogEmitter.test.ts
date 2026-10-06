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

const turn = (skipped: string[], direction = 1, seq = 1) => ({ skipped, direction, seq });

describe("createPlayLogEmitter", () => {
	let lines: LogLine[];
	let emitter: ReturnType<typeof createPlayLogEmitter>;

	beforeEach(() => {
		({ lines, emitter } = setup());
		escaped.length = 0;
	});

	it("logs a skip per skipped seat, escalating on the same player", async () => {
		emitter.noteTurn(turn(["Ann"]));
		emitter.noteTurn(turn(["Ann"]));
		emitter.noteTurn(turn(["Ann"]));
		await flush();

		expect(lines.map((line) => line.key)).toEqual([
			"log_skip",
			"log_skip_again_2",
			"log_skip_third"
		]);
	});

	it("threads the turn seq so a streak pool actually rotates (Review Focus)", async () => {
		emitter.noteTurn(turn(["Ann"], 1, 1));
		emitter.noteTurn(turn(["Ann"], 1, 1));
		emitter.noteTurn(turn(["Ann"], 1, 1));
		emitter.noteTurn(turn(["Ann"], 1, 1));
		emitter.noteTurn(turn(["Ann"], 1, 2));
		await flush();

		expect(lines.map((line) => line.key)).toEqual([
			"log_skip",
			"log_skip_again_2",
			"log_skip_third",
			"log_skip_streak_2",
			"log_skip_streak"
		]);
	});

	it("logs one line per skipped seat on a single turn, in order", async () => {
		emitter.noteTurn(turn(["Ann", "Bob"]));
		await flush();
		expect(lines.map((line) => line.params.name)).toEqual(["Ann", "Bob"]);
	});

	it("does not log a reverse for the first direction seen", async () => {
		emitter.noteTurn(turn([], -1));
		await flush();
		expect(lines).toEqual([]);
	});

	it("logs a reverse when the direction flips, escalating on repeats", async () => {
		emitter.noteTurn(turn([], 1));
		emitter.noteTurn(turn([], -1));
		emitter.noteTurn(turn([], 1));
		emitter.noteTurn(turn([], -1));
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			"log_reverse",
			"log_reverse_back",
			"log_reverse_again"
		]);
	});

	it("logs a reverse from a direction event, even for the first direction seen", async () => {
		emitter.noteDirection({ direction: -1, seq: 1 });
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_reverse"]);
	});

	it("does not log the same reverse again when the turn advance follows", async () => {
		emitter.noteDirection({ direction: -1, seq: 1 });
		emitter.noteTurn(turn([], -1, 2));
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_reverse"]);
	});

	it("logs one reverse when the turn advance comes before the direction event", async () => {
		emitter.noteTurn(turn([], 1, 1));
		emitter.noteTurn(turn([], -1, 2));
		emitter.noteDirection({ direction: -1, seq: 3 });
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_reverse"]);
	});

	it("logs two direction events that no turn advance separates", async () => {
		emitter.noteDirection({ direction: -1, seq: 1 });
		emitter.noteDirection({ direction: 1, seq: 2 });
		emitter.noteTurn(turn([], 1, 3));
		await flush();
		expect(lines).toHaveLength(2);
		expect(lines[0]!.key).toBe("log_reverse");
		expect(lines[1]!.key.startsWith("log_reverse_back")).toBe(true);
	});

	it("holds a play's lines until its card has landed", async () => {
		let land!: () => void;
		const landed = new Promise<void>((resolve) => (land = resolve));
		emitter.notePlay({ player: "Ann", kind: "uni:blue_5", color: "blue" }, landed, { seq: 1 });
		emitter.noteTurn(turn(["Bob"]));
		await flush();
		expect(lines).toEqual([]);

		land();
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_play", "log_skip"]);
	});

	it("a plain play between skips breaks the skip run", async () => {
		emitter.noteTurn(turn(["Ann"]));
		emitter.notePlay({ player: "Bob", kind: "uni:red_3", color: "red" }, Promise.resolve(), {
			seq: 2
		});
		emitter.noteTurn(turn(["Ann"]));
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_skip", "log_play", "log_skip"]);
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
		expect(lines.map((line) => line.key)).toEqual(["log_play", "log_play"]);
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
		expect(lines.map((line) => line.key)).toEqual([expect.stringMatching(/^log_auto_play/)]);
		expect(lines[0].params).toEqual({ name: "Ann", kind: "uni:red_skip", color: "red" });
	});

	it("logs a wild colour reaction alongside the play", async () => {
		emitter.notePlay({ player: "Ann", kind: "uni:wild", color: "red" }, Promise.resolve(), {
			seq: 4
		});
		await flush();
		expect(lines[0].key).toBe("log_play");
		expect(lines[1].key).toMatch(/^log_wild_reaction_(1|first_2)$/);
		expect(lines[1].params).toEqual({ name: "Ann", color: "red", prevColor: "" });
	});

	it("logs a wild colour reaction for a uni wild", async () => {
		emitter.notePlay({ player: "Ann", kind: "uni:wild", color: "red" }, Promise.resolve(), {
			seq: 4
		});
		await flush();
		expect(lines[1].key).toMatch(/^log_wild_reaction_(1|first_2)$/);
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
		expect(lines[0].key).toBe("log_play");
		expect(lines[1].key).toMatch(/^log_wild_reaction_(1|first_2)$/);
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
		expect(lines[0].key).toMatch(/^log_auto_play/);
		expect(lines[1].key).toMatch(/^log_wild_reaction_(1|first_2)$/);
	});

	it("logs a near-win reaction carried by the play", async () => {
		emitter.notePlay({ player: "Ann", kind: "uni:red_5", color: "red" }, Promise.resolve(), {
			seq: 6,
			nearWin: true
		});
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			"log_play",
			expect.stringMatching(/^log_near_win/)
		]);
		expect(lines[1].params).toEqual({ name: "Ann" });
	});

	it("logs a reshuffle", async () => {
		emitter.noteReshuffle({ seq: 7 });
		await flush();
		expect(lines.map((line) => line.key)).toEqual([expect.stringMatching(/^log_reshuffle/)]);
	});

	it("logs a keep without leaking the kept card (Review Focus)", async () => {
		emitter.noteKeep("Ann", 9);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([expect.stringMatching(/^log_keep_drawn/)]);
		expect(lines[0].params).toEqual({ name: "Ann" });
	});

	it("logs a win and each later placement once", async () => {
		emitter.noteGameEnd("Ann", ["Ann"]);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([expect.stringMatching(/^log_win/)]);

		emitter.noteGameEnd("Ann", ["Ann", "Bob"]);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			expect.stringMatching(/^log_win/),
			expect.stringMatching(/^log_left_behind/)
		]);
		expect(lines[1].params).toEqual({ name: "Bob", place: 2 });

		emitter.noteGameEnd("Ann", ["Ann", "Bob"]);
		await flush();
		expect(lines).toHaveLength(2);
	});

	it("words places up to the finisher count as finishing and the rest as left behind", async () => {
		emitter.noteGameEnd("Ann", ["Ann", "Bob", "Cy"], 2);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			expect.stringMatching(/^log_win/),
			expect.stringMatching(/^log_finish/),
			expect.stringMatching(/^log_left_behind/)
		]);
		expect(lines[2].params).toEqual({ name: "Cy", place: 3 });
	});

	it("reset clears the game-end trackers", async () => {
		emitter.noteGameEnd("Ann", ["Ann", "Bob"]);
		await flush();
		emitter.reset();
		emitter.noteGameEnd("Ann", ["Ann", "Bob"]);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			expect.stringMatching(/^log_win/),
			expect.stringMatching(/^log_left_behind/),
			expect.stringMatching(/^log_win/),
			expect.stringMatching(/^log_left_behind/)
		]);
	});

	it("logs a wild once its colour is already known", async () => {
		emitter.notePlay({ player: "Ann", kind: "uni:wild", color: "red" }, Promise.resolve(), {
			seq: 4
		});
		await flush();
		expect(lines[0].key).toBe("log_play");
		expect(lines[1].key).toMatch(/^log_wild_reaction_(1|first_2)$/);
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
		expect(lines.map((line) => line.key)).toEqual(["log_play", "log_draw_stack"]);
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

		const stacks = lines.filter((line) => line.key === "log_draw_stack");
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
		emitter.noteTurn(turn(["[fx=shake]x[/fx]"]));
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

	it("a reset mid-streak makes the next skip a first skip (Review Focus 5)", async () => {
		emitter.noteTurn(turn(["Ann"]));
		emitter.noteTurn(turn(["Ann"]));
		await flush();
		emitter.reset();
		emitter.noteTurn(turn(["Ann"]));
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_skip", "log_skip_again_2", "log_skip"]);
	});

	it("a reset forgets the direction baseline and drops queued lines", async () => {
		let land!: () => void;
		const landed = new Promise<void>((resolve) => (land = resolve));
		emitter.noteTurn(turn([], 1));
		emitter.notePlay({ player: "Ann", kind: "uni:red_skip", color: "red" }, landed, { seq: 1 });
		emitter.noteTurn(turn(["Bob"]));
		emitter.reset();
		land();
		emitter.noteTurn(turn([], -1));
		await flush();
		expect(lines).toEqual([]);
	});

	it("carries the skip actor onto the emitted skip event", async () => {
		emitter.noteTurn({ skipped: ["Bob"], direction: 1, seq: 5, actor: "Ann" });
		await flush();
		const line = lines[0];
		expect(line.key).toBe("log_skip");
		// INFO: the actor is not part of LogLine.params yet, so it is observed at
		//       the escape seam the emitter runs it through.
		expect(escaped).toContain("Ann");
	});

	it("logs a blocked retort when the near-win target is skipped", async () => {
		emitter.notePlay({ player: "Bob", kind: "uni:red_5", color: "red" }, Promise.resolve(), {
			seq: 1,
			nearWin: true
		});
		emitter.notePlay({ player: "Ann", kind: "uni:red_5", color: "red" }, Promise.resolve(), {
			seq: 2
		});
		emitter.noteTurn({ skipped: ["Bob"], direction: 1, seq: 3, actor: "Ann" });
		await flush();

		const skipIndex = lines.findIndex((line) => line.key === "log_skip");
		const blockedIndex = lines.findIndex((line) => line.key.startsWith("log_blocked_"));
		expect(skipIndex).toBeGreaterThanOrEqual(0);
		expect(blockedIndex).toBeGreaterThan(skipIndex);
		expect(lines[blockedIndex].params).toEqual({ blocker: "Ann", target: "Bob" });
	});

	it("logs a blocked retort when the near-win target is penalty-drawn", async () => {
		emitter.notePlay({ player: "Bob", kind: "uni:red_5", color: "red" }, Promise.resolve(), {
			seq: 1,
			nearWin: true
		});
		emitter.notePlay({ player: "Ann", kind: "uni:red_draw2", color: "red" }, Promise.resolve(), {
			seq: 2
		});
		emitter.noteDebt("Bob", 2);
		await flush();

		const drawIndex = lines.findIndex((line) => line.key === "log_draw_stack");
		const blockedIndex = lines.findIndex((line) => line.key.startsWith("log_blocked_"));
		expect(drawIndex).toBeGreaterThanOrEqual(0);
		expect(blockedIndex).toBeGreaterThan(drawIndex);
		expect(lines[blockedIndex].params).toEqual({ blocker: "Ann", target: "Bob" });
	});

	it("takes the blocker of a skip from the event, not from the turn guess", async () => {
		emitter.notePlay({ player: "Bob", kind: "uni:red_5", color: "red" }, Promise.resolve(), {
			seq: 1,
			nearWin: true
		});
		emitter.noteSkipped({ target: "Bob", actor: "Cy", seq: 3 });
		emitter.noteTurn({ skipped: ["Bob"], direction: 1, seq: 4, actor: "Ann" });
		await flush();

		expect(lines.filter((line) => line.key === "log_skip")).toHaveLength(1);
		const blocked = lines.find((line) => line.key.startsWith("log_blocked_"));
		expect(blocked?.params).toEqual({ blocker: "Cy", target: "Bob" });
	});

	it("takes the blocker of a skip from the event when the turn comes first", async () => {
		emitter.notePlay({ player: "Bob", kind: "uni:red_5", color: "red" }, Promise.resolve(), {
			seq: 1,
			nearWin: true
		});
		emitter.noteTurn({ skipped: ["Bob"], direction: 1, seq: 4, actor: "Ann" });
		emitter.noteSkipped({ target: "Bob", actor: "Cy", seq: 3 });
		await flush();

		expect(lines.filter((line) => line.key === "log_skip")).toHaveLength(1);
		const blocked = lines.find((line) => line.key.startsWith("log_blocked_"));
		expect(blocked?.params).toEqual({ blocker: "Cy", target: "Bob" });
	});

	it("logs a repeated skip of one seat once for each event", async () => {
		emitter.noteSkipped({ target: "Ann", seq: 1 });
		emitter.noteTurn(turn(["Ann"], 1, 2));
		emitter.noteSkipped({ target: "Ann", seq: 3 });
		emitter.noteTurn(turn(["Ann"], 1, 4));
		await flush();

		expect(lines.filter((line) => line.key.startsWith("log_skip"))).toHaveLength(2);
	});

	it("logs a later skip after an event whose turn advance never came", async () => {
		emitter.noteSkipped({ target: "Ann", seq: 1 });
		emitter.settleSkips();
		emitter.noteTurn(turn(["Ann"], 1, 2));
		await flush();

		expect(lines.filter((line) => line.key.startsWith("log_skip"))).toHaveLength(2);
	});
});
