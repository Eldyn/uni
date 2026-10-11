import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { buildLogLines } from "$lib/chat/playLog/logBuilder";
import type { LogEvent } from "$lib/chat/playLog/logEvent";
import { readStreaks } from "$lib/chat/playLog/serverStreaks";
import {
	BindingRegistry,
	clientEventOf,
	presentEvent,
	type EventEnvelope
} from "$lib/presentation/eventBindings";

const FIXTURE_DIR = resolve(import.meta.dirname, "../../../../../tests/fixtures/play_log_parity");
const SEQUENCES = Array.from({ length: 80 }, (_, index) => index + 1);
const VIEWER = { seat: "ann" };

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
}

interface Outcome {
	keys: string[];
	params: string[];
	lineCount: number;
}

const registries = new Map<string, BindingRegistry>();

function registryOf(row: string): BindingRegistry {
	let registry = registries.get(row);
	if (registry === undefined) {
		const body = JSON.parse(readFileSync(resolve(FIXTURE_DIR, `${row}.json`), "utf8"));
		registry = new BindingRegistry();
		registry.ingestDefs({ presentations: [{ id: `parity:${row}`, ...body }] });
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

function bindingOutcome(row: string, testCase: ParityCase): Outcome {
	const registry = registryOf(row);
	return summarize(
		SEQUENCES.map((sequence) =>
			presentEvent(registry, clientEventOf({ ...testCase.wire, sequence }), VIEWER)
				.map((output) => output.line)
				.filter((line) => line !== null)
		)
	);
}

function expectParity(row: string, testCase: ParityCase): void {
	const core = coreOutcome(testCase);
	const binding = bindingOutcome(row, testCase);
	expect(binding.keys).toEqual(core.keys);
	expect(binding.params).toEqual(core.params);
}

interface Row {
	row: string;
	/** Set when the bindings cannot express the whole row: the cases then fail. */
	gap?: string;
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

const wildCase = (run: number, name = `wild run ${run}`): ParityCase => ({
	name,
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

const placementCase = (name: string, place: number, isElimination: boolean): ParityCase => ({
	name,
	core: { kind: "elimination", player: "cat", place, finished: !isElimination },
	wire: {
		id: "placement_set",
		payload: { target: "cat", place, is_elimination: isElimination }
	}
});

const cardWire = (id: string): ParityCase["wire"] => ({
	id,
	actor: "ann",
	payload: { player: "ann", card: 42, from_zone_ordinal: 0, trigger_summary: "" }
});

const ROWS: Row[] = [
	{
		row: "play",
		gap: "card kind and color live in the compact card integer",
		cases: [
			{
				name: "play line",
				core: { kind: "play", player: "ann", cardKind: "uni:number", color: "red" },
				wire: cardWire("card_played")
			}
		]
	},
	{
		row: "auto_play",
		gap: "card kind and color live in the compact card integer",
		cases: [
			{
				name: "auto play line",
				core: { kind: "auto_play", player: "ann", cardKind: "uni:number", color: "red" },
				wire: cardWire("auto_played")
			}
		]
	},
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
			{
				name: "first place takes no line",
				core: null,
				wire: { id: "placement_set", payload: { target: "ann", place: 1, is_elimination: false } }
			}
		]
	},
	{
		row: "wild",
		cases: [wildCase(1), wildCase(2), wildCase(3), wildCase(6)]
	}
];

describe("play log parity: core rules against fixture bindings", () => {
	for (const { row, gap, cases } of ROWS) {
		describe(row, () => {
			for (const testCase of cases) {
				const run = gap === undefined ? it : it.fails;
				const title = gap === undefined ? testCase.name : `${testCase.name} (GAP: ${gap})`;
				run(title, () => expectParity(row, testCase));
			}
		});
	}
});

describe("play log parity: documented gaps (see parity-gaps.md)", () => {
	it.fails("blocked: a blocked event without an actor posts no line", () => {
		const testCase: ParityCase = {
			name: "blocked without actor",
			core: { kind: "blocked", player: "bob" },
			wire: { id: "blocked", payload: { target: "bob" } }
		};
		expectParity("blocked", testCase);
	});

	it.fails("elimination: a finisher beyond race_target reads as left behind", () => {
		const raceTarget = 1;
		const place = 3;
		const finished = place <= raceTarget;
		const testCase: ParityCase = {
			name: "place beyond race target",
			core: { kind: "elimination", player: "cat", place, finished },
			wire: { id: "placement_set", payload: { target: "cat", place, is_elimination: false } }
		};
		expectParity("elimination", testCase);
	});

	it.fails("wild: a color change that no wild play caused posts no line", () => {
		const event = clientEventOf({
			id: "active_type_changed",
			actor: "ann",
			sequence: 1,
			payload: { color: "green", previous_color: "red" },
			context: { "uni:wild_run": 1, "uni:last_wild_color": { last: "red" } }
		});
		const lines = presentEvent(registryOf("wild"), event, VIEWER).filter(
			(output) => output.line !== null
		);
		expect(lines).toHaveLength(0);
	});

	it.fails("wild: the first wild of a match keeps prevColor as an empty string", () => {
		const testCase = wildCase(1, "first wild without previous color");
		testCase.wire.context = {
			"uni:wild_run": 1,
			"uni:last_wild_color": { last: "blue" }
		};
		expectParity("wild", testCase);
	});

	it.fails("draw: a missing per-seat run reads as 0 in the drawRun param", () => {
		const testCase = drawCase({ name: "draw without run entry" });
		testCase.wire.context = { "uni:total_draws": 3, "uni:draw_run": {} };
		expectParity("draw", testCase);
	});

	it.skip("near_win: the line keeps its place behind a pending wild (ordering, not a set)", () => {
		// The core emitter holds the line until the wild color lands. A binding
		// posts at the event, so no set comparison can show the order.
	});
});
