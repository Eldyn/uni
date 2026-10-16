import { describe, expect, it } from "vitest";
import {
	BindingRegistry,
	clientEventOf,
	presentEvent,
	type CardResolver,
	type ClientEvent,
	type EventEnvelope,
	type Viewer
} from "$lib/presentation/eventBindings";

const presentationFiles = import.meta.glob<Record<string, unknown>>(
	"../../../../../mods/uni/presentations/*.json",
	{ eager: true, import: "default" }
);

const SEQUENCES = Array.from({ length: 80 }, (_, index) => index + 1);
const VIEWER: Viewer = { seat: "ann" };
const WILD_TAG = "uni:wild";

const CARD_TABLE: Record<number, { kind: string; color: string }> = {
	44: { kind: "uni:wild", color: "white" }
};
const resolveCard: CardResolver = (bits) => CARD_TABLE[bits];

const SKIP_STREAK_POOL = ["uni:log.skip_streak", "uni:log.skip_streak_2"];
const WILD_SECOND = ["uni:log.wild_reaction_second_1", "uni:log.wild_reaction_second_2"];
const WILD_THIRD = ["uni:log.wild_reaction_third_1", "uni:log.wild_reaction_third_2"];

/** The shipped uni log bindings as the wire carries them, the way the match
 *  registry ingests them. */
function shippedRegistry(): BindingRegistry {
	const registry = new BindingRegistry();
	registry.ingestDefs({
		presentations: Object.entries(presentationFiles).map(([path, binding]) => ({
			id: `uni:${path.slice(path.lastIndexOf("/") + 1).replace(/\.json$/, "")}`,
			...(binding as Record<string, unknown>)
		}))
	});
	return registry;
}

const REGISTRY = shippedRegistry();

/** The one event of a sequence: the same id and the same context, as two
 *  clients both receive it. */
type EventFactory = (sequence: number) => ClientEvent;

const eventOf = (wire: Omit<EventEnvelope, "sequence">, extra?: Partial<ClientEvent>) =>
	(sequence: number): ClientEvent => ({ ...clientEventOf({ ...wire, sequence }), ...extra });

/** The line keys the registry posts for one event, per sequence. */
function linesPerSequence(event: EventFactory): string[][] {
	return SEQUENCES.map((sequence) =>
		presentEvent(REGISTRY, event(sequence), VIEWER, resolveCard)
			.map((output) => output.line)
			.filter((line) => line !== null)
			.map((line) => line.key)
	);
}

/** The key of the one line an event posts, per sequence. */
function keysPerSequence(event: EventFactory): string[] {
	return linesPerSequence(event).map((keys) => keys[0] ?? "");
}

const skipOf = (run: number): EventFactory =>
	eventOf({
		id: "turn_skipped",
		actor: "ann",
		payload: { target: "bob" },
		context: { "uni:skip_run": run }
	});

const wildChangeOf = (run: number): EventFactory =>
	eventOf(
		{
			id: "active_type_changed",
			actor: "ann",
			payload: { color: "blue", previous_color: "white" },
			context: {
				"uni:wild_run": run,
				"uni:last_wild_color": run === 1 ? { last: "blue" } : { last: "blue", previous: "red" }
			}
		},
		{
			causeEvent: {
				id: "card_played",
				payload: { card: 44, player: "ann", tags: [WILD_TAG, "uni:action"] }
			}
		}
	);

describe("two clients telling the same joke", () => {
	it("picks the same key for the same event on every client", () => {
		// INFO: the variant pick salts on the binding id and the sequence, so
		//       two clients that see the same event pick the same line.
		for (const run of [1, 2, 3, 4, 7]) {
			const skip = linesPerSequence(skipOf(run));
			expect(skip, `skip run ${run}`).toEqual(linesPerSequence(skipOf(run)));
			for (const keys of skip) {
				expect(keys, `skip run ${run}`).toHaveLength(1);
				expect(keys[0], `skip run ${run}`).toMatch(/^uni:log\.skip/);
			}
		}
	});

	it("rotates deterministically within a multi-key pool", () => {
		const keys = keysPerSequence(skipOf(4));
		expect(new Set(keys).size).toBeGreaterThan(1);
		for (const key of keys) {
			expect(SKIP_STREAK_POOL).toContain(key);
		}
	});

	it("keeps two clients in step through the wild second and third chains", () => {
		for (const [run, pool] of [
			[2, WILD_SECOND],
			[3, WILD_THIRD]
		] as const) {
			const lines = linesPerSequence(wildChangeOf(run));
			const reactions = lines.map((keys) => keys.filter((key) => key.includes("wild_reaction")));
			for (const keys of lines) {
				// the play line and the reaction line post in registration order
				expect(keys).toHaveLength(2);
				expect(keys[0]).toBe("uni:log.play");
			}
			for (const keys of reactions) {
				expect(keys).toHaveLength(1);
				expect(pool).toContain(keys[0]);
			}
			expect(new Set(reactions.map((keys) => keys[0])).size).toBeGreaterThan(1);
		}
	});
});
