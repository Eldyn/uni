import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { buildLogLines, REACTION_RULES } from "$lib/chat/playLog/logBuilder";
import type { LogEvent } from "$lib/chat/playLog/logEvent";
import { readStreaks } from "$lib/chat/playLog/serverStreaks";
import {
	BindingRegistry,
	clientEventOf,
	presentEvent,
	type CardResolver,
	type EventEnvelope
} from "$lib/presentation/eventBindings";

const FIXTURE_DIR = resolve(import.meta.dirname, "../../../../../tests/fixtures/play_log_parity");
const SEQUENCES = Array.from({ length: 80 }, (_, index) => index + 1);
const VIEWER = { seat: "ann" };
const WILD_TAG = "uni:wild";

const CARD_TABLE: Record<number, { kind: string; color: string }> = {
	42: { kind: "uni:number", color: "red" },
	43: { kind: "uni:skip", color: "green" },
	44: { kind: "uni:wild", color: "white" }
};
const resolveCard: CardResolver = (bits) => CARD_TABLE[bits];

type CoreInput = LogEvent extends infer Event
	? Event extends LogEvent
		? Omit<Event, keyof { seq: number }>
		: never
	: never;

interface ParityCase {
	name: string;
	/** The core event; null when the core rules post no line for this wire event. */
	core: CoreInput | null;
	wire: Omit<EventEnvelope, "sequence">;
	/** The seat whose entry the per-seat draw count reads. */
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

/** One fixture file holds one binding. A row can use more than one file. */
const ROW_FIXTURES: Record<string, string[]> = {
	play: ["play", "play_wild"]
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

function coreOutcome(testCase: ParityCase): Outcome {
	if (testCase.core === null) return { keys: [], params: [], lineCount: 0 };
	const streak = readStreaks(testCase.wire.context, testCase.seat);
	return summarize(
		SEQUENCES.map((seq) => buildLogLines({ ...testCase.core, seq } as LogEvent, streak))
	);
}

/** The binding lines of a sequence of wire events; only the last carries the cause. */
function bindingOutcomeOf(
	row: string,
	steps: ParityCase["wire"][],
	causeEvent?: ParityCase["causeEvent"]
): Outcome {
	const registry = registryOf(row);
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

function bindingOutcome(row: string, testCase: ParityCase): Outcome {
	return bindingOutcomeOf(row, [testCase.wire], testCase.causeEvent);
}

function expectParity(row: string, testCase: ParityCase): void {
	const core = coreOutcome(testCase);
	const binding = bindingOutcome(row, testCase);
	expect(binding.keys).toEqual(core.keys);
	expect(binding.params).toEqual(core.params);
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

describe("play log parity: core rules against fixture bindings", () => {
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
		const core = coreOutcome({
			name: "wild play",
			core: { kind: "play", player: "ann", cardKind: "uni:wild", color: "blue" },
			wire: wildPlayWire()
		});
		const binding = bindingOutcomeOf(
			"play",
			[wildPlayWire(), wildColorWire("blue")],
			wildPlayCause()
		);
		expect(binding.keys).toEqual(core.keys);
		expect(binding.params).toEqual(core.params);
		expect(binding.lineCount).toBe(core.lineCount);
		expect(paramsOfOutcome(binding).map((entry) => entry.color)).toEqual(["blue"]);
	});

	it("play: the card_played of a wild posts no line", () => {
		expect(bindingOutcomeOf("play", [wildPlayWire()]).lineCount).toBe(0);
	});

	it("play: a color change with no wild cause posts no play line", () => {
		for (const causeEvent of [
			undefined,
			wildPlayCause(["action"]),
			{ id: "auto_played", payload: { card: 44, player: "ann", tags: [WILD_TAG] } }
		]) {
			expect(bindingOutcomeOf("play", [wildColorWire("green")], causeEvent).lineCount).toBe(0);
		}
	});

	it("play: a wild reset to white or to an empty color posts no play line", () => {
		for (const color of ["white", ""]) {
			expect(bindingOutcomeOf("play", [wildColorWire(color)], wildPlayCause()).lineCount).toBe(0);
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

describe("play log parity: accepted differences (see parity-gaps.md)", () => {
	it("auto_play: an auto-played wild posts one line with the registry color (accepted)", () => {
		// INFO: AutoPlayCard pushes active_type_changed before card_played and
		//       auto_played. The color change has no tagged cause of this card, so
		//       play_wild stays quiet; the tagged card_played posts no play line.
		const registry = registryOfFiles(["play", "play_wild", "auto_play"]);
		const steps = [
			{ wire: wildColorWire("blue"), causeEvent: undefined },
			{ wire: wildPlayWire(), causeEvent: undefined },
			{ wire: cardWire("auto_played", 44), causeEvent: undefined }
		];
		const lines = steps.flatMap(({ wire, causeEvent }) =>
			presentEvent(
				registry,
				{ ...clientEventOf({ ...wire, sequence: 1 }), causeEvent },
				VIEWER,
				resolveCard
			).filter((output) => output.line !== null)
		);
		expect(lines).toHaveLength(1);
		expect(lines[0]?.line?.params.color).toBe("white");
		const core = coreOutcome({
			name: "auto wild",
			core: { kind: "auto_play", player: "ann", cardKind: "uni:wild", color: "blue" },
			wire: cardWire("auto_played", 44)
		});
		expect(paramsOfOutcome(core).map((entry) => entry.color)).toEqual(["blue"]);
	});

	it.skip("near_win: the line keeps its place behind a pending wild (ordering, not a set)", () => {
		// The core emitter holds the line until the wild color lands. A binding
		// posts at the event, so no set comparison can show the order. Spec D10:
		// the event order is the contract.
	});
});

describe("play log parity: row table guard", () => {
	it("the fixture rows are the rule ids of the core, in the same set", () => {
		const fixtureRows = new Set(ROWS.map(({ row }) => row));
		const coreRows = new Set(REACTION_RULES.map((rule) => rule.id));
		expect([...fixtureRows].sort()).toEqual([...coreRows].sort());
	});

	it("every row has a fixture file with a binding", () => {
		for (const { row, cases } of ROWS) {
			for (const { wire } of cases) {
				expect(registryOf(row).forEvent(wire.id).length).toBeGreaterThan(0);
			}
		}
	});
});
