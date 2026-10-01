import { describe, expect, it, beforeEach } from "vitest";
import { createPlayLogEmitter } from "../../chat/playLog/playLogEmitter";
import type { LogLine } from "../../chat/playLog/logEvent";

const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

function setup() {
	const lines: LogLine[] = [];
	const emitter = createPlayLogEmitter((line) => lines.push(line));
	return { lines, emitter };
}

const turn = (skipped: string[], direction = 1) => ({ skipped, direction });

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
		emitter.notePlay({ player: "Ann", type: "blue", value: "5" }, landed);
		emitter.noteTurn(turn(["Bob"]));
		await flush();
		expect(lines).toEqual([]);

		land();
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_skip"]);
	});

	it("a plain play between skips breaks the skip run", async () => {
		emitter.noteTurn(turn(["Ann"]));
		emitter.notePlay({ player: "Bob", type: "red", value: "3" }, Promise.resolve());
		emitter.noteTurn(turn(["Ann"]));
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_skip", "log_skip"]);
	});

	it("the skip card itself does not break its own run", async () => {
		emitter.notePlay({ player: "Bob", type: "red", value: "skip" }, Promise.resolve());
		emitter.noteTurn(turn(["Ann"]));
		emitter.notePlay({ player: "Cy", type: "red", value: "skip" }, Promise.resolve());
		emitter.noteTurn(turn(["Ann"]));
		await flush();
		expect(lines.map((line) => line.key)).toEqual(["log_skip", "log_skip_again"]);
	});

	it("logs a wild with the chosen colour", async () => {
		emitter.notePlay({ player: "Ann", type: "white", value: "jolly" }, Promise.resolve(), "red");
		await flush();
		expect(lines).toEqual([
			{ kind: "log", key: "log_wild", params: { name: "Ann", color: "red" }, tier: 0 }
		]);
	});

	it("logs a pending wild once its colour arrives", async () => {
		emitter.notePlay({ player: "Ann", type: "white", value: "jolly" }, Promise.resolve());
		await flush();
		expect(lines).toEqual([]);

		emitter.noteWildColor("blue");
		await flush();
		expect(lines.map((line) => line.params)).toEqual([{ name: "Ann", color: "blue" }]);
	});

	it("logs a draw stack from a +N play followed by the debt status", async () => {
		emitter.notePlay({ player: "Ann", type: "green", value: "+2" }, Promise.resolve());
		emitter.noteDebt("Bob", 6);
		await flush();
		expect(lines).toEqual([
			{
				kind: "log",
				key: "log_draw_stack",
				params: { name: "Ann", victim: "Bob", amount: 2, total: 6 },
				tier: 0
			}
		]);
	});

	it("ignores a debt status with no preceding +N play", async () => {
		emitter.noteDebt("Bob", 2);
		await flush();
		expect(lines).toEqual([]);
	});

	it("escapes hostile names so they cannot inject markup (Review Focus 1)", async () => {
		emitter.noteTurn(turn(["[fx=shake]x[/fx]"]));
		emitter.notePlay({ player: "[c=red]Ann", type: "green", value: "+2" }, Promise.resolve());
		emitter.noteDebt("[b]Bob", 4);
		await flush();

		for (const line of lines) {
			for (const value of Object.values(line.params)) {
				if (typeof value !== "string") continue;
				expect(value).not.toMatch(/(^|[^\\])\[/);
			}
		}
		expect(lines).toHaveLength(2);
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
		emitter.notePlay({ player: "Ann", type: "red", value: "skip" }, landed);
		emitter.noteTurn(turn(["Bob"]));
		emitter.reset();
		land();
		emitter.noteTurn(turn([], -1));
		await flush();
		expect(lines).toEqual([]);
	});
});
