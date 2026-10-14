import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import {
	BindingRegistry,
	clientEventOf,
	presentEvent,
	type CardResolver,
	type CauseRecall,
	type ClientEvent,
	type EventEnvelope
} from "$lib/presentation/eventBindings";

const FIXTURE_DIR = resolve(import.meta.dirname, "../../../../../tests/fixtures/play_log_parity");
const SHIPPED_DIR = resolve(import.meta.dirname, "../../../../../mods/uni/presentations");
const SEQUENCES = Array.from({ length: 80 }, (_, index) => index + 1);
const VIEWER = { seat: "ann" };
const WILD_TAG = "uni:wild";

const CARD_TABLE: Record<number, { kind: string; color: string }> = {
	42: { kind: "uni:number", color: "red" },
	43: { kind: "uni:skip", color: "green" },
	44: { kind: "uni:wild", color: "white" }
};
const resolveCard: CardResolver = (bits) => CARD_TABLE[bits];

/**
 * The frozen core expectation the wire was built from. The core rules are
 * gone; this stays as the documentation of what each wire means, so nothing
 * reads it.
 */
type CoreInput = { kind: string; [field: string]: unknown };

interface ParityCase {
	name: string;
	core: CoreInput | null;
	wire: Omit<EventEnvelope, "sequence">;
	/** The seat whose entry the per-seat draw count reads (likewise unread now). */
	seat?: string;
	/** The event that caused the wire event, as the runner remembers it. */
	causeEvent?: { id: string; payload: Record<string, unknown> };
}

interface Outcome {
	keys: string[];
	params: string[];
	lineCount: number;
}

const registries = new Map<string, BindingRegistry>();
const shippedRegistries = new Map<string, BindingRegistry>();

/** One fixture file holds one binding. A row can use more than one file. */
const ROW_FIXTURES: Record<string, string[]> = {
	play: ["play", "play_wild"],
	auto_play: ["auto_play", "auto_play_wild"]
};

function registryOfFiles(files: string[]): BindingRegistry {
	const registry = new BindingRegistry();
	const presentations = files.map((file) => ({
		id: `parity:${file}`,
		...JSON.parse(readFileSync(resolve(FIXTURE_DIR, `${file}.json`), "utf8"))
	}));
	registry.ingestDefs({ presentations });
	return registry;
}

function registryOf(row: string): BindingRegistry {
	let registry = registries.get(row);
	if (registry === undefined) {
		registry = registryOfFiles(ROW_FIXTURES[row] ?? [row]);
		registries.set(row, registry);
	}
	return registry;
}

/**
 * The shipped bindings of a row: the same file names read from the mod, with
 * the `id` the server injects at defs-build time (`uni:<stem>`). Each side
 * keeps its own ids: the variant pick salts on the binding id, so sharing
 * ids would couple the two rotations.
 */
function shippedRegistryOfFiles(files: string[]): BindingRegistry {
	const registry = new BindingRegistry();
	const presentations = files.map((file) => ({
		id: `uni:${file}`,
		...JSON.parse(readFileSync(resolve(SHIPPED_DIR, `${file}.json`), "utf8"))
	}));
	registry.ingestDefs({ presentations });
	return registry;
}

function shippedRegistryOf(row: string): BindingRegistry {
	let registry = shippedRegistries.get(row);
	if (registry === undefined) {
		registry = shippedRegistryOfFiles(ROW_FIXTURES[row] ?? [row]);
		shippedRegistries.set(row, registry);
	}
	return registry;
}

/**
 * A cause memory like bindingRunner's: remembers each event of the chain by
 * sequence, so an `ancestor` own_log negation can walk it.
 */
function recallOf(
	chain: { sequence: number; id: string; payload: Record<string, unknown>; cause?: number }[]
): CauseRecall {
	const recent = new Map(
		chain.map((event) => [event.sequence, { id: event.id, payload: event.payload, cause: event.cause }])
	);
	return (sequence: number) => recent.get(sequence);
}

function sortedParams(params: Record<string, string | number>): string {
	return JSON.stringify(Object.entries(params).sort(([a], [b]) => a.localeCompare(b)));
}

function summarize(lines: { key: string; params: Record<string, string | number> }[][]): Outcome {
	const keys = new Set<string>();
	const params = new Set<string>();
	let lineCount = 0;
	for (const batch of lines) {
		for (const line of batch) {
			keys.add(line.key);
			params.add(sortedParams(line.params));
			lineCount += 1;
		}
	}
	return { keys: [...keys].sort(), params: [...params].sort(), lineCount };
}

/** The binding lines of a sequence of wire events; only the last carries the cause. */
function outcomeOf(
	registry: BindingRegistry,
	steps: ParityCase["wire"][],
	causeEvent?: ParityCase["causeEvent"]
): Outcome {
	return summarize(
		SEQUENCES.map((sequence) =>
			steps.flatMap((wire, index) =>
				presentEvent(
					registry,
					{
						...clientEventOf({ ...wire, sequence }),
						causeEvent: index === steps.length - 1 ? causeEvent : undefined
					},
					VIEWER,
					resolveCard
				)
					.map((output) => output.line)
					.filter((line) => line !== null)
			)
		)
	);
}

function fixtureOutcomeOf(
	row: string,
	steps: ParityCase["wire"][],
	causeEvent?: ParityCase["causeEvent"]
): Outcome {
	return outcomeOf(registryOf(row), steps, causeEvent);
}

function shippedOutcomeOf(
	row: string,
	steps: ParityCase["wire"][],
	causeEvent?: ParityCase["causeEvent"]
): Outcome {
	return outcomeOf(shippedRegistryOf(row), steps, causeEvent);
}

function fixtureOutcome(row: string, testCase: ParityCase): Outcome {
	return fixtureOutcomeOf(row, [testCase.wire], testCase.causeEvent);
}

function shippedOutcome(row: string, testCase: ParityCase): Outcome {
	return shippedOutcomeOf(row, [testCase.wire], testCase.causeEvent);
}

function expectParity(row: string, testCase: ParityCase): void {
	const fixture = fixtureOutcome(row, testCase);
	const shipped = shippedOutcome(row, testCase);
	expect(shipped.keys).toEqual(fixture.keys);
	expect(shipped.params).toEqual(fixture.params);
}

/** The shipped line keys of one event, for the single-event own_log checks. */
function shippedKeys(row: string, event: ClientEvent, recall?: CauseRecall): string[] {
	return presentEvent(shippedRegistryOf(row), event, VIEWER, resolveCard, recall)
		.map((output) => output.line)
		.filter((line): line is NonNullable<typeof line> => line !== null)
		.map((line) => line.key);
}

interface Row {
	row: string;
	cases: ParityCase[];
}

const skipCase = (run: number): ParityCase => ({
	name: `skip run ${run}`,
	core: { kind: "skip", player: "bob", actor: "ann" },
	wire: {
		id: "turn_skipped",
		actor: "ann",
		payload: { target: "bob" },
		context: { "uni:skip_run": run }
	}
});

const reverseCase = (run: number): ParityCase => ({
	name: `reverse run ${run}`,
	core: { kind: "reverse" },
	wire: {
		id: "direction_changed",
		actor: "ann",
		payload: { direction: -1 },
		context: { "uni:reverse_run": run }
	}
});

interface DrawShape {
	name: string;
	count?: number;
	handSize?: number;
	totalDraws?: number;
	drawRun?: number;
}

const drawCase = ({
	name,
	count = 1,
	handSize = 5,
	totalDraws = 3,
	drawRun = 1
}: DrawShape): ParityCase => ({
	name,
	seat: "ann",
	core: { kind: "draw", player: "ann", count, penalty: false, handSize },
	wire: {
		id: "cards_drawn",
		actor: "ann",
		payload: {
			target: "ann",
			count,
			source_pile: "draw",
			hand_size_after: handSize,
			from_debt: false
		},
		context: { "uni:total_draws": totalDraws, "uni:draw_run": { ann: drawRun } }
	}
});

const wildCause = { id: "card_played", payload: { tags: ["wild", "action", WILD_TAG] } };

const wildCase = (run: number, name = `wild run ${run}`): ParityCase => ({
	name,
	causeEvent: wildCause,
	core: { kind: "wild", player: "ann", color: "blue" },
	wire: {
		id: "active_type_changed",
		actor: "ann",
		payload: { color: "blue", previous_color: "white" },
		context: {
			"uni:wild_run": run,
			"uni:last_wild_color": { last: "blue", previous: "red" }
		}
	}
});

/** The server rule for the wire field `finished`. */
const finishedOf = (place: number, isElimination: boolean, raceTarget: number): boolean =>
	!isElimination && place <= Math.max(1, raceTarget);

const placementCase = (
	name: string,
	place: number,
	isElimination: boolean,
	raceTarget = 2
): ParityCase => {
	const finished = finishedOf(place, isElimination, raceTarget);
	return {
		name,
		core: { kind: "elimination", player: "cat", place, finished },
		wire: {
			id: "placement_set",
			payload: { target: "cat", place, is_elimination: isElimination, finished }
		}
	};
};

const cardWire = (id: string, card: number): ParityCase["wire"] => ({
	id,
	actor: "ann",
	payload: { player: "ann", card, from_zone_ordinal: 0, trigger_summary: "" }
});

const cardCases = (kind: "play" | "auto_play", id: string): ParityCase[] =>
	[42, 43].map((card) => ({
		name: `${kind} line, card ${card}`,
		core: {
			kind,
			player: "ann",
			cardKind: CARD_TABLE[card]!.kind,
			color: CARD_TABLE[card]!.color
		},
		wire: cardWire(id, card)
	}));

const ROWS: Row[] = [
	{ row: "play", cases: cardCases("play", "card_played") },
	{ row: "auto_play", cases: cardCases("auto_play", "auto_played") },
	{ row: "skip", cases: [0, 1, 2, 3, 4, 7].map(skipCase) },
	{ row: "reverse", cases: [0, 1, 2, 3, 4, 7].map(reverseCase) },
	{
		row: "draw",
		cases: [
			drawCase({ name: "generic" }),
			drawCase({ name: "first", totalDraws: 1 }),
			drawCase({ name: "many", drawRun: 4 }),
			drawCase({ name: "many run 9", drawRun: 9 }),
			drawCase({ name: "big hand", handSize: 10 }),
			drawCase({ name: "large", count: 4 }),
			drawCase({ name: "big hand beats large", handSize: 12, count: 5, totalDraws: 1 }),
			drawCase({ name: "large beats first", count: 4, totalDraws: 1 }),
			drawCase({ name: "first beats many", totalDraws: 1, drawRun: 5 })
		]
	},
	{
		row: "blocked",
		cases: [
			{
				name: "blocked by a seat",
				core: { kind: "blocked", player: "bob", blocker: "ann" },
				wire: { id: "blocked", actor: "ann", payload: { target: "bob" } }
			},
			{
				name: "blocked without an actor posts no line",
				core: null,
				wire: { id: "blocked", payload: { target: "bob" } }
			}
		]
	},
	{
		row: "reshuffle",
		cases: [
			{
				name: "reshuffle",
				core: { kind: "reshuffle" },
				wire: { id: "reshuffle", payload: { draw_size: 20, discard_size: 0 } }
			}
		]
	},
	{
		row: "keep",
		cases: [
			{
				name: "keep",
				core: { kind: "keep", player: "ann" },
				wire: { id: "drawn_card_kept", actor: "ann", payload: { target: "ann" } }
			}
		]
	},
	{
		row: "near_win",
		cases: [
			{
				name: "near win",
				core: { kind: "near_win", player: "ann" },
				wire: { id: "near_win_entered", actor: "ann", payload: { target: "ann" } }
			}
		]
	},
	{
		row: "win",
		cases: [
			{
				name: "win",
				core: { kind: "win", player: "ann" },
				wire: { id: "match_ended", payload: { target: "ann", targets: ["ann", "bob"] } }
			},
			{
				name: "no winner",
				core: null,
				wire: { id: "match_ended", payload: { target: "", targets: [] } }
			}
		]
	},
	{
		row: "elimination",
		cases: [
			placementCase("finished", 2, false),
			placementCase("left behind by elimination", 3, true),
			placementCase("a finisher beyond race_target reads as left behind", 3, false, 1),
			placementCase("race_target 0 still finishes place 1 only", 2, false, 0),
			{
				name: "first place takes no line",
				core: null,
				wire: {
					id: "placement_set",
					payload: { target: "ann", place: 1, is_elimination: false, finished: true }
				}
			}
		]
	},
	{
		row: "wild",
		cases: [wildCase(0), wildCase(1), wildCase(2), wildCase(3), wildCase(6)]
	}
];

describe("play log parity: the shipped bindings against the frozen fixtures", () => {
	for (const { row, cases } of ROWS) {
		describe(row, () => {
			for (const testCase of cases) {
				it(testCase.name, () => expectParity(row, testCase));
			}
		});
	}
});

describe("play log parity: cases that closed with the new binding features", () => {
	it("wild: a color change that no wild play caused posts no line (causes include a synthetic id mismatch)", () => {
		const event = clientEventOf({
			id: "active_type_changed",
			actor: "ann",
			sequence: 1,
			payload: { color: "green", previous_color: "red" },
			context: { "uni:wild_run": 1, "uni:last_wild_color": { last: "red" } }
		});
		for (const causeEvent of [
			undefined,
			{ id: "card_played", payload: { tags: ["action"] } },
			// synthetic: the id differs from card_played; not a wire shape
			{ id: "auto_played", payload: { tags: [WILD_TAG] } }
		]) {
			const lines = presentEvent(
				registryOf("wild"),
				{ ...event, causeEvent },
				VIEWER,
				resolveCard
			).filter((output) => output.line !== null);
			expect(lines).toHaveLength(0);
		}
	});

	it("wild: the first wild of a match keeps prevColor as an empty string", () => {
		const testCase = wildCase(1, "first wild without previous color");
		testCase.wire.context = {
			"uni:wild_run": 1,
			"uni:last_wild_color": { last: "blue" }
		};
		expectParity("wild", testCase);
	});

	it("draw: a missing per-seat run reads as 0 in the drawRun param", () => {
		const testCase = drawCase({ name: "draw without run entry" });
		testCase.wire.context = { "uni:total_draws": 3, "uni:draw_run": {} };
		expectParity("draw", testCase);
	});
});

const wildPlayWire = (): ParityCase["wire"] => {
	const wire = cardWire("card_played", 44);
	return { ...wire, payload: { ...wire.payload, tags: ["wild", "action", WILD_TAG] } };
};

const wildColorWire = (color: string): ParityCase["wire"] => ({
	id: "active_type_changed",
	actor: "ann",
	payload: { color, previous_color: "white" }
});

const wildPlayCause = (tags: string[] = ["wild", "action", WILD_TAG]) => ({
	id: "card_played",
	payload: { card: 44, player: "ann", tags }
});

const paramsOfOutcome = (outcome: Outcome): Record<string, unknown>[] =>
	outcome.params.map((entry) => Object.fromEntries(JSON.parse(entry) as [string, unknown][]));

describe("play log parity: the wild play line through the cause event", () => {
	it("play: a wild with a known color posts the same line, with the chosen color", () => {
		const fixture = fixtureOutcomeOf(
			"play",
			[wildPlayWire(), wildColorWire("blue")],
			wildPlayCause()
		);
		const shipped = shippedOutcomeOf(
			"play",
			[wildPlayWire(), wildColorWire("blue")],
			wildPlayCause()
		);
		expect(shipped.keys).toEqual(fixture.keys);
		expect(shipped.params).toEqual(fixture.params);
		expect(shipped.lineCount).toBe(fixture.lineCount);
		expect(paramsOfOutcome(shipped).map((entry) => entry.color)).toEqual(["blue"]);
	});

	it("play: the card_played of a wild posts no line", () => {
		expect(fixtureOutcomeOf("play", [wildPlayWire()]).lineCount).toBe(0);
		expect(shippedOutcomeOf("play", [wildPlayWire()]).lineCount).toBe(0);
	});

	it("play: a color change with no wild cause posts no play line", () => {
		for (const causeEvent of [
			undefined,
			wildPlayCause(["action"]),
			{ id: "auto_played", payload: { card: 44, player: "ann", tags: [WILD_TAG] } }
		]) {
			expect(fixtureOutcomeOf("play", [wildColorWire("green")], causeEvent).lineCount).toBe(0);
			expect(shippedOutcomeOf("play", [wildColorWire("green")], causeEvent).lineCount).toBe(0);
		}
	});

	it("play: a wild reset to white or to an empty color posts no play line", () => {
		for (const color of ["white", ""]) {
			expect(fixtureOutcomeOf("play", [wildColorWire(color)], wildPlayCause()).lineCount).toBe(0);
			expect(shippedOutcomeOf("play", [wildColorWire(color)], wildPlayCause()).lineCount).toBe(0);
		}
	});

	it("play: the play line and the wild reaction line post in registration order", () => {
		// INFO: two bindings on one event post in the order they are registered.
		const keysFor = (files: string[]) => {
			const event = {
				...clientEventOf({ ...wildColorWire("blue"), sequence: 1, context: { "uni:wild_run": 1 } }),
				causeEvent: wildPlayCause()
			};
			return presentEvent(registryOfFiles(files), event, VIEWER, resolveCard).map(
				(output) => output.line?.key
			);
		};
		const forward = keysFor(["play_wild", "wild"]);
		expect(forward).toHaveLength(2);
		expect(forward[0]).toBe("uni:log.play");
		expect(forward[1]).toMatch(/^uni:log\.wild_reaction/);
		expect(keysFor(["wild", "play_wild"])).toEqual([...forward].reverse());
	});
});

/** The auto-played wild wire: its `tags` name the wild kind. */
const autoPlayedWire = (): ParityCase["wire"] => {
	const wire = cardWire("auto_played", 44);
	return { ...wire, payload: { ...wire.payload, tags: [WILD_TAG] } };
};

const autoPlayedCause = () => ({
	id: "auto_played",
	payload: { card: 44, player: "ann", tags: [WILD_TAG] }
});

describe("play log parity: an auto-played wild posts through the colour change (G7)", () => {
	// INFO: the engine opens a causal scope per play, so an auto play emits
	// `card_played` -> `auto_played` and only then its graph events: the
	// colour change is caused by the auto_played event, whose tags name the
	// wild. The `auto_play` binding skips a wild (negated `has_tag`), and the
	// `auto_play_wild` fixture posts the play line with the chosen colour.
	const shipped = shippedRegistryOf("auto_play");

	it("auto_play: an auto-played wild posts one line with the chosen colour", () => {
		const steps = [
			{ wire: autoPlayedWire(), causeEvent: undefined },
			{ wire: wildColorWire("blue"), causeEvent: autoPlayedCause() }
		];
		const lines = steps.flatMap(({ wire, causeEvent }) =>
			presentEvent(
				shipped,
				{ ...clientEventOf({ ...wire, sequence: 1 }), causeEvent },
				VIEWER,
				resolveCard
			).filter((output) => output.line !== null)
		);
		expect(lines).toHaveLength(1);
		expect(lines[0]?.line?.key).toBe("uni:log.auto_play_1");
		expect(lines[0]?.line?.params.name).toBe("ann");
		expect(lines[0]?.line?.params.color).toBe("blue");
		// The core rule posts the same line with the same colour (the rule
		// semantics the fixture froze): the shipped pool holds only
		// `auto_play_1`, so the variant is fixed whatever the id salt is.
		const fixture = fixtureOutcomeOf(
			"auto_play",
			[autoPlayedWire(), wildColorWire("blue")],
			autoPlayedCause()
		);
		const shippedSummary = shippedOutcomeOf(
			"auto_play",
			[autoPlayedWire(), wildColorWire("blue")],
			autoPlayedCause()
		);
		expect(shippedSummary.keys).toEqual(fixture.keys);
		expect(shippedSummary.params).toEqual(fixture.params);
		expect(paramsOfOutcome(shippedSummary).map((entry) => entry.color)).toEqual(["blue"]);
	});

	it("auto_play: a colour change an untagged event caused posts no play line", () => {
		for (const causeEvent of [
			undefined,
			{ id: "card_played", payload: { tags: [WILD_TAG] } },
			{ id: "auto_played", payload: { tags: ["action"] } }
		]) {
			expect(fixtureOutcomeOf("auto_play", [wildColorWire("green")], causeEvent).lineCount).toBe(0);
			expect(shippedOutcomeOf("auto_play", [wildColorWire("green")], causeEvent).lineCount).toBe(0);
		}
	});
});

describe("shipped bindings: core behaviours the fixtures do not cover", () => {
	// The core wild rule groups reactions by run: the first wild of a match,
	// the second, and the third and later — each with its own variant pool.
	const reactionPool: Record<number, string[]> = {
		1: ["uni:log.wild_reaction_1", "uni:log.wild_reaction_first_2"],
		2: ["uni:log.wild_reaction_second_1", "uni:log.wild_reaction_second_2"],
		3: ["uni:log.wild_reaction_third_1", "uni:log.wild_reaction_third_2"]
	};

	for (const run of [1, 2, 3]) {
		it(`auto-played wild reaction: run ${run} posts one wild_reaction line with name, color and prevColor`, () => {
			const event = {
				...clientEventOf({
					...wildColorWire("blue"),
					sequence: 7,
					context: {
						"uni:wild_run": run,
						"uni:last_wild_color":
							run === 1 ? { last: "blue" } : { last: "blue", previous: "red" }
					}
				}),
				causeEvent: autoPlayedCause()
			};
			const lines = presentEvent(shippedRegistryOf("wild"), event, VIEWER, resolveCard)
				.map((output) => output.line)
				.filter((line): line is NonNullable<typeof line> => line !== null);
			expect(lines).toHaveLength(1);
			expect(lines[0]!.key).toMatch(/^uni:log\.wild_reaction/);
			expect(reactionPool[run]).toContain(lines[0]!.key);
			expect(lines[0]!.params).toEqual({
				name: "ann",
				color: "blue",
				prevColor: run === 1 ? "" : "red"
			});
		});
	}

	const ownLogPlay = (sequence: number, cause?: number) => ({
		sequence,
		id: "card_played",
		payload: { player: "ann", card: 42, tags: ["uni:own_log"] },
		cause
	});
	const plainPlay = (sequence: number, cause?: number) => ({
		sequence,
		id: "card_played",
		payload: { player: "ann", card: 42, tags: [] as string[] },
		cause
	});

	it("own_log: a card_played with the own_log tag posts no play line", () => {
		const ownEvent = clientEventOf({
			id: "card_played",
			actor: "ann",
			sequence: 10,
			payload: { player: "ann", card: 42, tags: ["uni:own_log"] }
		});
		expect(shippedKeys("play", ownEvent, recallOf([ownLogPlay(10)]))).toEqual([]);
		const plainEvent = clientEventOf({
			id: "card_played",
			actor: "ann",
			sequence: 10,
			payload: { player: "ann", card: 42, tags: [] as string[] }
		});
		expect(shippedKeys("play", plainEvent, recallOf([plainPlay(10)]))).toEqual(["uni:log.play"]);
	});

	it("own_log: a turn_skipped that an own_log play caused posts no line", () => {
		const skipAt = (cause: number) =>
			({
				...clientEventOf({
					id: "turn_skipped",
					actor: "ann",
					sequence: 11,
					payload: { target: "bob" },
					context: { "uni:skip_run": 1 }
				}),
				cause
			}) satisfies ClientEvent;
		const ownChain = [
			ownLogPlay(10),
			{ sequence: 11, id: "turn_skipped", payload: { target: "bob" }, cause: 10 }
		];
		expect(shippedKeys("skip", skipAt(10), recallOf(ownChain))).toEqual([]);
		const plainChain = [
			plainPlay(10),
			{ sequence: 11, id: "turn_skipped", payload: { target: "bob" }, cause: 10 }
		];
		expect(shippedKeys("skip", skipAt(10), recallOf(plainChain))).toEqual(["uni:log.skip"]);
	});

	it("own_log: a near_win_entered that an own_log play caused posts no line", () => {
		const enterAt = (cause: number) =>
			({
				...clientEventOf({
					id: "near_win_entered",
					actor: "ann",
					sequence: 13,
					payload: { target: "ann" }
				}),
				cause
			}) satisfies ClientEvent;
		const ownChain = [
			ownLogPlay(10),
			{ sequence: 13, id: "near_win_entered", payload: { target: "ann" }, cause: 10 }
		];
		expect(shippedKeys("near_win", enterAt(10), recallOf(ownChain))).toEqual([]);
		const plainChain = [
			plainPlay(10),
			{ sequence: 13, id: "near_win_entered", payload: { target: "ann" }, cause: 10 }
		];
		// The variant pick salts on the binding id, so the shipped pool can
		// name any of its three lines; the parity table above pins the set.
		const keys = shippedKeys("near_win", enterAt(10), recallOf(plainChain));
		expect(keys).toHaveLength(1);
		expect(keys[0]).toMatch(/^uni:log\.near_win_/);
	});

	it("own_log: a cards_drawn with an own_log play as ancestor cause posts no line", () => {
		const drawAt = (cause: number) =>
			({
				...clientEventOf({
					id: "cards_drawn",
					actor: "ann",
					sequence: 12,
					payload: { target: "ann", count: 2, hand_size_after: 7 },
					context: { "uni:total_draws": 2, "uni:draw_run": { ann: 1 } }
				}),
				cause
			}) satisfies ClientEvent;
		const ownChain = [
			ownLogPlay(10),
			{ sequence: 11, id: "mod:curse", payload: {}, cause: 10 },
			{
				sequence: 12,
				id: "cards_drawn",
				payload: { target: "ann", count: 2, hand_size_after: 7 },
				cause: 11
			}
		];
		expect(shippedKeys("draw", drawAt(11), recallOf(ownChain))).toEqual([]);
		const plainChain = [
			plainPlay(10),
			{ sequence: 11, id: "mod:curse", payload: {}, cause: 10 },
			{
				sequence: 12,
				id: "cards_drawn",
				payload: { target: "ann", count: 2, hand_size_after: 7 },
				cause: 11
			}
		];
		const keys = shippedKeys("draw", drawAt(11), recallOf(plainChain));
		expect(keys).toHaveLength(1);
		expect(keys[0]).toMatch(/^uni:log\.draw_/);
	});
});

describe("play log parity: accepted differences (see parity-gaps.md)", () => {
	it.skip("near_win: the line keeps its place behind a pending wild (ordering, not a set)", () => {
		// The core emitter holds the line until the wild color lands. A binding
		// posts at the event, so no set comparison can show the order. Spec D10:
		// the event order is the contract.
	});
});

describe("play log parity: shipped registry guard", () => {
	it("every row's shipped registry has a binding for every wire event the row uses", () => {
		for (const { row, cases } of ROWS) {
			for (const { wire } of cases) {
				expect(shippedRegistryOf(row).forEvent(wire.id).length).toBeGreaterThan(0);
			}
		}
	});
});
