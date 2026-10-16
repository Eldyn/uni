import { describe, expect, it } from "vitest";
import { replayTailToLog, type TailBindings, type TailEnvelope } from "../../chat/playLog/tailReplay";
import type { LogLine } from "../../chat/playLog/logEvent";
import { BindingRegistry, type Viewer } from "$lib/presentation/eventBindings";

const presentationFiles = import.meta.glob<Record<string, unknown>>(
	"../../../../../mods/uni/presentations/*.json",
	{ eager: true, import: "default" }
);

const RED_FIVE_BITS = 1000;
const WILD_BITS = 2000;
const RED_DRAW2_BITS = 3000;
/** Decision D2: a card with this tag writes its own log line. */
const OWN_LOG_TAG = "uni:own_log";
const WILD_TAG = "uni:wild";

const VIEWER: Viewer = { seat: "Ann" };

function resolveKind(bits: number): { kind: string; color: string } | undefined {
	if (bits === RED_FIVE_BITS) return { kind: "uni:red_5", color: "red" };
	if (bits === RED_DRAW2_BITS) return { kind: "uni:draw2", color: "red" };
	if (bits === WILD_BITS) return { kind: "uni:wild", color: "white" };
	return undefined;
}

/**
 * The shipped uni log bindings, parsed the way the match registry ingests them:
 * each file with the `id` the server injects at defs-build time.
 */
function shippedFiles(): Record<string, unknown>[] {
	return Object.entries(presentationFiles).map(([path, binding]) => ({
		id: `uni:${path.slice(path.lastIndexOf("/") + 1).replace(/\.json$/, "")}`,
		...(binding as Record<string, unknown>)
	}));
}

function registryOf(presentations: Record<string, unknown>[]): BindingRegistry {
	const registry = new BindingRegistry();
	registry.ingestDefs({ presentations });
	return registry;
}

function envelope(
	sequence: number,
	id: string,
	payload: Record<string, unknown>,
	actor?: string,
	context?: Record<string, unknown>,
	cause?: number
): TailEnvelope {
	return { sequence, id, actor, replayed: true, payload, context, cause };
}

/** A replay posts the lines of the shipped bindings; nothing else posts. */
const SHIPPED_BINDINGS: TailBindings = {
	registry: registryOf(shippedFiles()),
	getViewer: () => VIEWER
};

function replay(tail: TailEnvelope[], bindings: TailBindings = SHIPPED_BINDINGS): LogLine[] {
	const lines: LogLine[] = [];
	replayTailToLog(tail, resolveKind, (line) => lines.push(line), bindings);
	return lines;
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

/** A stacking mod beside the shipped bindings: both post their own line. */
const STACK_BINDINGS: TailBindings = {
	registry: registryOf([...shippedFiles(), STACK_BINDING]),
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
				{ player: "Ann", card: RED_FIVE_BITS, from_zone_ordinal: 0, tags: [] },
				"Ann"
			)
		]);
		expect(lines.map((line) => line.key)).toEqual(["uni:log.play"]);
		expect(lines[0].params).toEqual({ name: "Ann", kind: "uni:red_5", color: "red" });
	});

	it("waits for the color of a wild and logs the play and the wild line", () => {
		const lines = replay([
			envelope(
				0,
				"card_played",
				{
					player: "Ann",
					card: WILD_BITS,
					from_zone_ordinal: 0,
					tags: [WILD_TAG, "uni:action"]
				},
				"Ann"
			),
			envelope(1, "active_type_changed", { color: "blue", previous_color: "red" }, "Ann", undefined, 0)
		]);
		const play = lines.find((line) => line.key === "uni:log.play");
		expect(play?.params).toMatchObject({ color: "blue" });
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.play",
			expect.stringMatching(/^uni:log\.wild_reaction/)
		]);
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
			"uni:log.keep_drawn",
			expect.stringMatching(/^uni:log\.win/)
		]);
	});

	it("ignores an id that has no binding", () => {
		const lines = replay([
			envelope(0, "turn_advance", { from: "Ann", to: "Bob" }),
			envelope(1, "some_mod:thing", {})
		]);
		expect(lines).toEqual([]);
	});

	it("posts a degraded play line when the card cannot be resolved (accepted: narrow defs-lag trigger)", () => {
		// INFO: ACCEPTED DEGRADATION (follow-up item 36). The core path dropped
		//       the line when the card stayed unknown; a binding cannot express
		//       "required", so it posts anyway and only the params it cannot
		//       read (kind, color) are missing. `withCardName` skips `cardName`
		//       without a string `kind`, so the chat shows raw `{kind}` /
		//       `{cardName}` / `{color}` placeholders. The trigger is narrow —
		//       the live path resolves bits for every viewer through the shared
		//       defs registry, so only a late joiner whose defs lag, or an
		//       unknown kind index, can reach this. See
		//       docs/superpowers/tickets/mod-play-log/follow-ups.md#36.
		const lines = replay([
			envelope(2, "card_played", { player: "Ann", card: 7, from_zone_ordinal: 0, tags: [] }, "Ann")
		]);
		expect(lines.map((line) => line.key)).toEqual(["uni:log.play"]);
		expect(lines[0].params).toEqual({ name: "Ann" });
	});

	it("is deterministic: the same tail gives the same lines", () => {
		const tail = [
			envelope(0, "turn_skipped", { target: "Bob" }, "Ann"),
			envelope(1, "turn_skipped", { target: "Bob" }, "Ann")
		];
		expect(replay(tail)).toEqual(replay(tail));
	});

	it("leaves the line of a debt draw to the stacking mod", () => {
		const debt = replay(
			[
				envelope(
					0,
					"status_applied",
					{ target: "Bob", status_kind: "uni:draw_debt", magnitude: 4 },
					"Ann"
				)
			],
			STACK_BINDINGS
		);
		expect(debt.map((line) => line.key)).toEqual([
			expect.stringMatching(/^draw_stacking:stack_small_/)
		]);
	});

	it("logs the near-win line from the near_win_entered event, after the play", () => {
		const lines = replay([
			envelope(
				0,
				"card_played",
				{ player: "Ann", card: RED_FIVE_BITS, from_zone_ordinal: 0, tags: [] },
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

	it("posts the lines in event order: the wild line and its play wait for the color", () => {
		// INFO: a binding posts at its own event, unlike the core emitter that
		//       held the lines back. Spec D10: the event order is the contract.
		const lines = replay([
			envelope(
				0,
				"card_played",
				{
					player: "Ann",
					card: WILD_BITS,
					from_zone_ordinal: 0,
					tags: [WILD_TAG, "uni:action"]
				},
				"Ann"
			),
			envelope(1, "near_win_entered", { target: "Ann" }, "Ann"),
			envelope(
				2,
				"active_type_changed",
				{ color: "blue", previous_color: "red" },
				"Ann",
				{ "uni:wild_run": 1 },
				0
			)
		]);
		expect(lines.map((line) => line.key)).toEqual([
			expect.stringMatching(/^uni:log\.near_win/),
			"uni:log.play",
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
			envelope(
				1,
				"card_played",
				{ player: "Bob", card: WILD_BITS, from_zone_ordinal: 0, tags: [WILD_TAG] },
				"Bob"
			),
			envelope(
				2,
				"active_type_changed",
				{ color: "green", previous_color: "red" },
				"Bob",
				{ "uni:wild_run": 3, "uni:last_wild_color": { last: "green", previous: "blue" } },
				1
			)
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
				{ player: "Ann", card: RED_DRAW2_BITS, from_zone_ordinal: 0, tags: [] },
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

		it("posts the stack line of a stacking response next to the play line", () => {
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
							tags: [WILD_TAG, "uni:action", OWN_LOG_TAG]
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
				envelope(
					0,
					"placement_set",
					{ target: "Bob", place: 2, is_elimination: true, finished: false },
					"Ann"
				),
				envelope(
					1,
					"placement_set",
					{ target: "Bob", place: 2, is_elimination: true, finished: false },
					"Ann"
				)
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
