import { describe, expect, it, beforeEach } from "vitest";
import { createPlayLogEmitter } from "../../chat/playLog/playLogEmitter";
import type { LogLine } from "../../chat/playLog/logEvent";

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
	});

	it("logs a skip per skipped seat, escalating on the same player", async () => {
		emitter.noteTurn(turn(["Ann"]));
		emitter.noteTurn(turn(["Ann"]));
		emitter.noteTurn(turn(["Ann"]));
		await flush();

		expect(lines.map((line) => line.key)).toEqual(["log_skip", "log_skip_again", "log_skip_third"]);
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
			"log_skip_again",
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

	it("holds a play's lines until its card has landed", async () => {
		let land!: () => void;
		const landed = new Promise<void>((resolve) => (land = resolve));
		emitter.notePlay({ player: "Ann", kind: "vanilla:blue_5", color: "blue" }, landed, { seq: 1 });
		emitter.noteTurn(turn(["Bob"]));
		await flush();
		expect(lines).toEqual([]);

		land();
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_play", "log_skip"]);
	});

	it("a plain play between skips breaks the skip run", async () => {
		emitter.noteTurn(turn(["Ann"]));
		emitter.notePlay({ player: "Bob", kind: "vanilla:red_3", color: "red" }, Promise.resolve(), {
			seq: 2
		});
		emitter.noteTurn(turn(["Ann"]));
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_skip", "log_play", "log_skip"]);
	});

	it("logs every play, including specials", async () => {
		emitter.notePlay({ player: "Ann", kind: "vanilla:red_skip", color: "red" }, Promise.resolve(), {
			seq: 1
		});
		emitter.notePlay(
			{ player: "Bob", kind: "vanilla:green_draw2", color: "green" },
			Promise.resolve(),
			{ seq: 2 }
		);
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_play", "log_play"]);
		expect(lines[1].params).toEqual({
			name: "Bob",
			kind: "vanilla:green_draw2",
			color: "green"
		});
	});

	it("logs a forced play as an auto play, not a play", async () => {
		emitter.notePlay(
			{ player: "Ann", kind: "vanilla:red_skip", color: "red", auto: true },
			Promise.resolve(),
			{
				seq: 3
			}
		);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([expect.stringMatching(/^log_auto_play/)]);
		expect(lines[0].params).toEqual({ name: "Ann", kind: "vanilla:red_skip", color: "red" });
	});

	it("logs a wild colour reaction alongside the play", async () => {
		emitter.notePlay({ player: "Ann", kind: "vanilla:wild", color: "red" }, Promise.resolve(), {
			seq: 4
		});
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_play", "log_wild_reaction_1"]);
		expect(lines[1].params).toEqual({ name: "Ann", color: "red" });
	});

	it("logs the wild colour reaction once the colour arrives", async () => {
		let land!: () => void;
		const landed = new Promise<void>((resolve) => (land = resolve));
		emitter.notePlay({ player: "Ann", kind: "vanilla:wild", color: "white" }, landed, { seq: 5 });
		emitter.noteWildColor("blue");
		land();
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_play", "log_wild_reaction_1"]);
		expect(lines[1].params).toEqual({ name: "Ann", color: "blue" });
	});

	it("keeps a forced wild as an auto play once its colour arrives", async () => {
		let land!: () => void;
		const landed = new Promise<void>((resolve) => (land = resolve));
		emitter.notePlay({ player: "Ann", kind: "vanilla:wild", color: "white", auto: true }, landed, {
			seq: 6
		});
		emitter.noteWildColor("blue");
		land();
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			expect.stringMatching(/^log_auto_play/),
			"log_wild_reaction_1"
		]);
	});

	it("logs a near-win reaction carried by the play", async () => {
		emitter.notePlay({ player: "Ann", kind: "vanilla:red_5", color: "red" }, Promise.resolve(), {
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

	it("logs a win and each later placement once", async () => {
		emitter.noteGameEnd("Ann", ["Ann"]);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([expect.stringMatching(/^log_win/)]);

		emitter.noteGameEnd("Ann", ["Ann", "Bob"]);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			expect.stringMatching(/^log_win/),
			expect.stringMatching(/^log_elimination/)
		]);
		expect(lines[1].params).toEqual({ name: "Bob", place: 2 });

		emitter.noteGameEnd("Ann", ["Ann", "Bob"]);
		await flush();
		expect(lines).toHaveLength(2);
	});

	it("reset clears the game-end trackers", async () => {
		emitter.noteGameEnd("Ann", ["Ann", "Bob"]);
		await flush();
		emitter.reset();
		emitter.noteGameEnd("Ann", ["Ann", "Bob"]);
		await flush();
		expect(lines.map((line) => line.key)).toEqual([
			expect.stringMatching(/^log_win/),
			expect.stringMatching(/^log_elimination/),
			expect.stringMatching(/^log_win/),
			expect.stringMatching(/^log_elimination/)
		]);
	});

	it("logs a wild once its colour is already known", async () => {
		emitter.notePlay({ player: "Ann", kind: "vanilla:wild", color: "red" }, Promise.resolve(), {
			seq: 4
		});
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_play", "log_wild_reaction_1"]);
		expect(lines[0].params).toEqual({ name: "Ann", kind: "vanilla:wild", color: "red" });
	});

	it("logs a play for a wild once its colour arrives", async () => {
		let land!: () => void;
		const landed = new Promise<void>((resolve) => (land = resolve));
		emitter.notePlay({ player: "Ann", kind: "vanilla:wild", color: "white" }, landed, { seq: 3 });
		await flush();
		expect(lines).toEqual([]);

		emitter.noteWildColor("red");
		land();
		await flush();
		expect(lines.map((line) => line.params)).toEqual([
			{ name: "Ann", kind: "vanilla:wild", color: "red" },
			{ name: "Ann", color: "red" }
		]);
	});

	it("logs a draw stack from a +N play followed by the debt status", async () => {
		emitter.notePlay(
			{ player: "Ann", kind: "vanilla:green_draw2", color: "green" },
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
			{ player: "Ann", kind: "vanilla:green_draw2", color: "green" },
			Promise.resolve(),
			{ seq: 5 }
		);
		emitter.noteDebt("Bob", 2);
		emitter.notePlay(
			{ player: "Bob", kind: "vanilla:wild_draw4", color: "red" },
			Promise.resolve(),
			{ seq: 6 }
		);
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
			{ player: "[c=red]Ann", kind: "vanilla:green_draw2", color: "green" },
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
		expect(lines.map((line) => line.key)).toEqual(["log_skip", "log_skip_again", "log_skip"]);
	});

	it("a reset forgets the direction baseline and drops queued lines", async () => {
		let land!: () => void;
		const landed = new Promise<void>((resolve) => (land = resolve));
		emitter.noteTurn(turn([], 1));
		emitter.notePlay({ player: "Ann", kind: "vanilla:red_skip", color: "red" }, landed, { seq: 1 });
		emitter.noteTurn(turn(["Bob"]));
		emitter.reset();
		land();
		emitter.noteTurn(turn([], -1));
		await flush();
		expect(lines).toEqual([]);
	});
});
