import { describe, expect, it } from "vitest";
import { replayTailToLog, type TailBindings, type TailEnvelope } from "../../chat/playLog/tailReplay";
import type { LogLine } from "../../chat/playLog/logEvent";
import { BindingRegistry, type Viewer } from "$lib/presentation/eventBindings";

const RED_FIVE_BITS = 1000;
const WILD_BITS = 2000;
const RED_DRAW2_BITS = 3000;
/** Decision D2: a card with this tag writes its own log line. */
const OWN_LOG_TAG = "uni:own_log";

const VIEWER: Viewer = { seat: "Ann" };

function resolveKind(bits: number): { kind: string; color: string } | undefined {
	if (bits === RED_FIVE_BITS) return { kind: "uni:red_5", color: "red" };
	if (bits === RED_DRAW2_BITS) return { kind: "uni:draw2", color: "red" };
	if (bits === WILD_BITS) return { kind: "uni:wild", color: "white" };
	return undefined;
}

function envelope(
	sequence: number,
	id: string,
	payload: Record<string, unknown>,
	actor?: string,
	context?: Record<string, unknown>,
	cause?: number
): TailEnvelope {
	return { sequence, seq: sequence, id, type: id, actor, replayed: true, payload, context, cause };
}

function replay(tail: TailEnvelope[], bindings?: TailBindings): LogLine[] {
	const lines: LogLine[] = [];
	replayTailToLog(tail, resolveKind, (line) => lines.push(line), bindings);
	return lines;
}

function registryOf(presentations: Record<string, unknown>[]): BindingRegistry {
	const registry = new BindingRegistry();
	registry.ingestDefs({ presentations });
	return registry;
}

/** A wild that carries `uni:own_log` reads its player from the play it caused. */
const OWN_LOG_WILD_BINDING = {
	id: "test:own_log_wild",
	def_hash: "0123456789abcdef",
	"uni:format_version": "1.0.0",
	"uni:binding": {
		"uni:event": "active_type_changed",
		"uni:select": "first",
		"uni:cases": [
			{
				"uni:tests": [
					{ "uni:test": "caused_by", "uni:value": "card_played", "uni:tag": "uni:wild" }
				],
				"uni:present": {
					"uni:log": {
						"uni:variants": ["test:own_log_wild"],
						"uni:params": {
							name: { "uni:field": "player", "uni:cause": true },
							color: { "uni:field": "color" }
						}
					}
				}
			}
		]
	}
};

/** A binding that also wants beats and sounds: a replay posts the line only. */
const ANIMATED_BINDING = {
	id: "test:animated",
	def_hash: "0123456789abcdef",
	"uni:format_version": "1.0.0",
	"uni:binding": {
		"uni:event": "status_applied",
		"uni:cases": [
			{
				"uni:present": {
					"uni:beats": [[{ op: "shake", target: "$payload.target" }]],
					"uni:sounds": [{ "uni:sound": "sfx.ui.tick" }],
					"uni:log": {
						"uni:variants": ["test:animated"],
						"uni:params": { total: { "uni:field": "magnitude" } }
					}
				}
			}
		]
	}
};

/**
 * The draw-stacking binding of `mods/draw_stacking/presentations/play_log.json`,
 * inline here so the test reads no file. It names the same event, the same
 * tests and the same variant keys.
 */
const STACK_BINDING = {
	id: "draw_stacking:play_log",
	def_hash: "0123456789abcdef",
	"uni:format_version": "1.0.0",
	"uni:binding": {
		"uni:event": "status_applied",
		"uni:select": "first",
		"uni:cases": [
			{
				"uni:tests": [
					{ "uni:test": "equals", "uni:field": "status_kind", "uni:value": "uni:draw_debt" },
					{ "uni:test": "gte", "uni:field": "magnitude", "uni:value": 8 }
				],
				"uni:present": {
					"uni:log": {
						"uni:variants": ["draw_stacking:stack_heavy_1"],
						"uni:params": {
							name: { "uni:actor": true },
							victim: { "uni:field": "target" },
							total: { "uni:field": "magnitude" }
						}
					}
				}
			},
			{
				"uni:tests": [
					{ "uni:test": "equals", "uni:field": "status_kind", "uni:value": "uni:draw_debt" }
				],
				"uni:present": {
					"uni:log": {
						"uni:variants": ["draw_stacking:stack_small_1", "draw_stacking:stack_small_2"],
						"uni:params": {
							name: { "uni:actor": true },
							victim: { "uni:field": "target" },
							total: { "uni:field": "magnitude" }
						}
					}
				}
			}
		]
	}
};

const STACK_BINDINGS: TailBindings = {
	registry: registryOf([STACK_BINDING]),
	getViewer: () => VIEWER
};

const MOD_BINDINGS: TailBindings = {
	registry: registryOf([OWN_LOG_WILD_BINDING, ANIMATED_BINDING]),
	getViewer: () => VIEWER
};

describe("replayTailToLog", () => {
	it("turns a retained play into a play line", () => {
		const lines = replay([
			envelope(
				0,
				"card_played",
				{ player: "Ann", card: RED_FIVE_BITS, from_zone_ordinal: 0 },
				"Ann"
			)
		]);
		expect(lines.map((line) => line.key)).toEqual(["uni:log.play"]);
		expect(lines[0].params).toMatchObject({ name: "Ann", kind: "uni:red_5", color: "red" });
	});

	it("waits for the color of a wild and logs the play and the wild line", () => {
		const lines = replay([
			envelope(0, "card_played", { player: "Ann", card: WILD_BITS, from_zone_ordinal: 0 }, "Ann"),
			envelope(1, "active_type_changed", { color: "blue", previous_color: "red" }, "Ann")
		]);
		expect(lines[0].params).toMatchObject({ color: "blue" });
		expect(lines.length).toBeGreaterThan(1);
	});

	it("logs a skip, a reverse, a keep and a win", () => {
		const lines = replay([
			envelope(0, "turn_skipped", { target: "Bob" }, "Ann"),
			envelope(1, "direction_changed", { direction: -1 }, "Ann"),
			envelope(2, "drawn_card_kept", { target: "Bob" }, "Bob"),
			envelope(3, "match_ended", { target: "Ann", targets: ["Ann", "Bob"] })
		]);
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.skip",
			"uni:log.reverse",
			expect.stringMatching(/^uni:log\.keep/),
			expect.stringMatching(/^uni:log\.win/)
		]);
	});

	it("ignores an id that has no log line and an unknown card", () => {
		const lines = replay([
			envelope(0, "turn_advance", { from: "Ann", to: "Bob" }),
			envelope(1, "some_mod:thing", {}),
			envelope(2, "card_played", { player: "Ann", card: 7, from_zone_ordinal: 0 })
		]);
		expect(lines).toEqual([]);
	});

	it("is deterministic: the same tail gives the same lines", () => {
		const tail = [
			envelope(0, "turn_skipped", { target: "Bob" }, "Ann"),
			envelope(1, "turn_skipped", { target: "Bob" }, "Ann")
		];
		expect(replay(tail)).toEqual(replay(tail));
	});

	it("logs a debt draw as a generic draw with no stack line", () => {
		const debt = replay([
			envelope(0, "cards_drawn", { target: "Bob", count: 4, from_debt: true }, "Bob")
		]);
		const plain = replay([envelope(0, "cards_drawn", { target: "Bob", count: 1 }, "Bob")]);
		expect(debt.map((line) => line.key)).toHaveLength(1);
		expect(debt[0].key).toMatch(/^uni:log\.draw_/);
		expect(debt[0].key).not.toMatch(/stack/);
		expect(plain[0].key).not.toMatch(/stack/);
	});

	it("logs the near-win line from the near_win_entered event, after the play", () => {
		const lines = replay([
			envelope(
				0,
				"card_played",
				{ player: "Ann", card: RED_FIVE_BITS, from_zone_ordinal: 0 },
				"Ann"
			),
			envelope(1, "near_win_entered", { target: "Ann" }, "Ann")
		]);
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.play",
			expect.stringMatching(/^uni:log\.near_win/)
		]);
		expect(lines[1].params).toEqual({ name: "Ann" });
	});

	it("keeps the near-win line of a wild between its play line and its wild line", () => {
		const lines = replay([
			envelope(0, "card_played", { player: "Ann", card: WILD_BITS, from_zone_ordinal: 0 }, "Ann"),
			envelope(1, "near_win_entered", { target: "Ann" }, "Ann"),
			envelope(2, "active_type_changed", { color: "blue", previous_color: "red" }, "Ann", {
				"uni:wild_run": 1
			})
		]);
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.play",
			expect.stringMatching(/^uni:log\.near_win/),
			expect.stringMatching(/^uni:log\.wild_reaction/)
		]);
	});

	it("logs the blocked line from the blocked event with the envelope actor", () => {
		const lines = replay([
			envelope(0, "turn_skipped", { target: "Bob" }, "Ann", { "uni:skip_run": 1 }),
			envelope(1, "blocked", { target: "Bob" }, "Ann")
		]);
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.skip",
			expect.stringMatching(/^uni:log\.blocked_/)
		]);
		expect(lines[1].params).toEqual({ blocker: "Ann", target: "Bob" });
	});

	it("logs no blocked line when the envelope has no actor", () => {
		const lines = replay([envelope(0, "blocked", { target: "Bob" })]);
		expect(lines).toEqual([]);
	});

	it("reads the draw counts and the wild run from the envelope context", () => {
		const lines = replay([
			envelope(
				0,
				"cards_drawn",
				{ target: "Ann", player: "Ann", count: 1, hand_size_after: 3, held: false },
				"Ann",
				{ "uni:draw_run": { Ann: 4, Bob: 1 }, "uni:total_draws": 5 }
			),
			envelope(1, "card_played", { player: "Bob", card: WILD_BITS, from_zone_ordinal: 0 }, "Bob"),
			envelope(2, "active_type_changed", { color: "green", previous_color: "red" }, "Bob", {
				"uni:wild_run": 3,
				"uni:last_wild_color": { last: "green", previous: "blue" }
			})
		]);
		expect(lines[0].key).toMatch(/^uni:log\.draw_many/);
		const wild = lines.find((line) => line.key.startsWith("uni:log.wild_reaction"));
		expect(wild?.key).toMatch(/^uni:log\.wild_reaction_third/);
		expect(wild?.params.prevColor).toBe("blue");
	});

	it("counts nothing itself: a tail with no context reads as no streak info", () => {
		const lines = replay([
			envelope(0, "turn_skipped", { target: "Bob" }, "Ann"),
			envelope(1, "turn_skipped", { target: "Bob" }, "Ann"),
			envelope(2, "turn_skipped", { target: "Bob" }, "Ann")
		]);
		expect(lines.map((line) => line.key)).toEqual(["uni:log.skip", "uni:log.skip", "uni:log.skip"]);
	});

	describe("with the presentation bindings", () => {
		const stackingTail = (): TailEnvelope[] => [
			envelope(
				0,
				"card_played",
				{ player: "Ann", card: RED_DRAW2_BITS, from_zone_ordinal: 0 },
				"Ann"
			),
			envelope(
				1,
				"status_applied",
				{ target: "Bob", status_kind: "uni:draw_debt", magnitude: 4 },
				"Ann",
				undefined,
				0
			)
		];

		it("posts the stack line of a stacking response next to the core play line", () => {
			const lines = replay(stackingTail(), STACK_BINDINGS);
			expect(lines.map((line) => line.key)).toEqual([
				"uni:log.play",
				expect.stringMatching(/^draw_stacking:stack_small_/)
			]);
			expect(lines[1].params).toEqual({ name: "Ann", victim: "Bob", total: 4 });
		});

		it("posts one wild line with the chosen colour when the card writes its own line", () => {
			const lines = replay(
				[
					envelope(
						0,
						"card_played",
						{
							player: "Ann",
							card: WILD_BITS,
							from_zone_ordinal: 0,
							tags: ["uni:wild", "uni:action", OWN_LOG_TAG]
						},
						"Ann"
					),
					envelope(
						1,
						"active_type_changed",
						{ color: "blue", previous_color: "red" },
						"Ann",
						undefined,
						0
					)
				],
				MOD_BINDINGS
			);
			expect(lines.map((line) => line.key)).toEqual(["test:own_log_wild"]);
			expect(lines[0].params).toEqual({ name: "Ann", color: "blue" });
		});

		it("posts no own-log line when the cause is not in the tail", () => {
			const lines = replay(
				[
					envelope(
						1,
						"active_type_changed",
						{ color: "blue", previous_color: "red" },
						"Ann",
						undefined,
						0
					)
				],
				MOD_BINDINGS
			);
			expect(lines).toEqual([]);
		});

		it("keeps a replayed event animation-free: only the log line posts", () => {
			const lines = replay(
				[
					envelope(
						0,
						"status_applied",
						{ target: "Bob", status_kind: "uni:draw_debt", magnitude: 4 },
						"Ann"
					)
				],
				MOD_BINDINGS
			);
			expect(lines.map((line) => line.key)).toEqual(["test:animated"]);
			expect(lines[0].params).toEqual({ total: 4 });
		});

		it("posts no second line for a repeated envelope", () => {
			const lines = replay([...stackingTail(), ...stackingTail()], STACK_BINDINGS);
			expect(lines.map((line) => line.key)).toEqual([
				"uni:log.play",
				expect.stringMatching(/^draw_stacking:stack_small_/)
			]);
		});

		it("posts no second reverse line for a direction change to the same direction", () => {
			const lines = replay([
				envelope(0, "direction_changed", { direction: -1 }, "Ann", { "uni:reverse_run": 1 }),
				envelope(1, "direction_changed", { direction: -1 }, "Ann", { "uni:reverse_run": 2 })
			]);
			expect(lines.map((line) => line.key)).toEqual([
				expect.stringMatching(/^uni:log\.reverse/)
			]);
		});

		it("posts no second elimination line for a repeat placement", () => {
			const lines = replay([
				envelope(0, "placement_set", { target: "Bob", place: 2, is_elimination: true }, "Ann"),
				envelope(1, "placement_set", { target: "Bob", place: 2, is_elimination: true }, "Ann")
			]);
			expect(lines.map((line) => line.key)).toEqual([
				expect.stringMatching(/^uni:log\.left_behind/)
			]);
		});

		it("posts no second win line for a repeated winner", () => {
			const lines = replay([
				envelope(0, "match_ended", { target: "Ann", targets: ["Ann", "Bob"] }),
				envelope(1, "match_ended", { target: "Ann", targets: ["Ann", "Bob"] })
			]);
			expect(lines.map((line) => line.key)).toEqual([expect.stringMatching(/^uni:log\.win/)]);
		});
	});
});
