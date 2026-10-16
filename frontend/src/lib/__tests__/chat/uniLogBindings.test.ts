import { readFileSync, readdirSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { BindingRegistry, presentEvent, type CardResolver } from "$lib/presentation/eventBindings";

const SHIPPED_DIR = resolve(import.meta.dirname, "../../../../../mods/uni/presentations");
const VIEWER = { seat: "ann" };

const resolveCard: CardResolver = () => undefined;

/**
 * The shipped uni log bindings as the wire carries them: each parsed file with
 * the `id` the server uses, which is the binding's own
 * `uni:binding.uni:description.uni:identifier` (`ParseBindingEntry` sets
 * `binding_id` from it, and both the registry and the `defs` packet are keyed by
 * it), so `uni:play_log`, not `uni:play`. The on-disk schema forbids the key.
 */
function shippedFiles(): Record<string, unknown>[] {
	return readdirSync(SHIPPED_DIR)
		.filter((file) => /^(skip|reverse|draw|blocked|reshuffle|keep|near_win|elimination|win|play|play_wild|auto_play|auto_play_wild|wild)\.json$/.test(file))
		.map((file) => {
			const binding = JSON.parse(readFileSync(resolve(SHIPPED_DIR, file), "utf8")) as {
				"uni:binding": { "uni:description": { "uni:identifier": string } };
			};
			const id = binding["uni:binding"]["uni:description"]["uni:identifier"];
			expect(id, `${file} declares no uni:identifier`).toBeTruthy();
			return { id, ...binding };
		});
}

/** The shipped uni log bindings, ingested the way the match registry ingests them. */
function shippedRegistry(): BindingRegistry {
	const registry = new BindingRegistry();
	registry.ingestDefs({ presentations: shippedFiles() });
	return registry;
}

/**
 * A cause memory like bindingRunner's: remembers each event of the chain by
 * sequence. The ancestor (`uni:ancestor: true`) own_log negations walk this
 * chain, so every own_log test builds one.
 */
function recallOf(
	chain: { sequence: number; id: string; payload: Record<string, unknown>; cause?: number }[]
) {
	const recent = new Map(
		chain.map((event) => [event.sequence, { id: event.id, payload: event.payload, cause: event.cause }])
	);
	return (sequence: number | undefined) =>
		sequence === undefined ? undefined : recent.get(sequence);
}

function lineKeys(
	registry: BindingRegistry,
	event: Parameters<typeof presentEvent>[1],
	recall?: (sequence: number | undefined) => unknown
): string[] {
	return presentEvent(registry, event, VIEWER, resolveCard, recall as never)
		.map((output) => output.line)
		.filter((line) => line !== null)
		.map((line) => line.key);
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
	payload: { player: "ann", card: 42, tags: [] },
	cause
});

describe("shipped uni log bindings: the event-shaped rules", () => {
	it("the nine event files ship with uni identifiers", () => {
		const identifiers = readDirShipped();
		for (const id of [
			"uni:skip_log", "uni:reverse_log", "uni:draw_log", "uni:blocked_log",
			"uni:reshuffle_log", "uni:keep_log", "uni:near_win_log", "uni:elimination_log", "uni:win_log"
		]) {
			expect(identifiers).toContain(id);
		}
	});

	it("a normal skip posts the skip line", () => {
		const keys = lineKeys(shippedRegistry(), {
			sequence: 11, id: "turn_skipped", actor: "ann",
			payload: { target: "bob" }, context: { "uni:skip_run": 1 }
		});
		expect(keys).toEqual(["uni:log.skip"]);
	});

	it("a skip a plain play caused posts the skip line", () => {
		const chain = [
			plainPlay(10),
			{ sequence: 11, id: "turn_skipped", payload: { target: "bob" }, cause: 10 }
		];
		const keys = lineKeys(
			shippedRegistry(),
			{ ...chain[1]!, context: { "uni:skip_run": 1 } },
			recallOf(chain)
		);
		expect(keys).toEqual(["uni:log.skip"]);
	});

	it("a skip an own_log play caused posts no line", () => {
		const chain = [
			ownLogPlay(10),
			{ sequence: 11, id: "turn_skipped", payload: { target: "bob" }, cause: 10 }
		];
		const keys = lineKeys(
			shippedRegistry(),
			{ ...chain[1]!, context: { "uni:skip_run": 1 } },
			recallOf(chain)
		);
		expect(keys).toEqual([]);
	});

	it("a normal draw posts a draw line", () => {
		const chain = [
			plainPlay(10),
			{
				sequence: 11,
				id: "cards_drawn",
				payload: { target: "ann", count: 1, hand_size_after: 5 },
				cause: 10
			}
		];
		const keys = lineKeys(
			shippedRegistry(),
			{ ...chain[1]!, context: { "uni:total_draws": 2, "uni:draw_run": { ann: 1 } } },
			recallOf(chain)
		);
		expect(keys).toHaveLength(1);
		expect(keys[0]).toMatch(/^uni:log\.draw_/);
	});

	it("a draw through a mod event after an own_log play posts no line", () => {
		const chain = [
			ownLogPlay(10),
			{ sequence: 11, id: "mod:curse", payload: {}, cause: 10 },
			{
				sequence: 12,
				id: "cards_drawn",
				payload: { target: "ann", count: 2, hand_size_after: 7 },
				cause: 11
			}
		];
		const keys = lineKeys(
			shippedRegistry(),
			{ ...chain[2]!, context: { "uni:total_draws": 2, "uni:draw_run": { ann: 1 } } },
			recallOf(chain)
		);
		expect(keys).toEqual([]);
	});

	it("a near-win an own_log play caused posts no line", () => {
		const chain = [
			ownLogPlay(10),
			{ sequence: 13, id: "near_win_entered", payload: { target: "ann" }, cause: 10 }
		];
		const keys = lineKeys(shippedRegistry(), { ...chain[1]! }, recallOf(chain));
		expect(keys).toEqual([]);
	});
});

describe("shipped uni log bindings: the card rules", () => {
	const CARD_TABLE: Record<number, { kind: string; color: string }> = {
		42: { kind: "uni:number", color: "red" },
		44: { kind: "uni:wild", color: "white" }
	};
	const resolvePlayCard = (bits: number) => CARD_TABLE[bits];

	const wildCause = {
		id: "card_played",
		payload: { card: 44, player: "ann", tags: ["wild", "action", "uni:wild"] }
	};
	const wildOwnCause = {
		id: "card_played",
		payload: { card: 44, player: "ann", tags: ["wild", "action", "uni:wild", "uni:own_log"] }
	};
	const autoCause = {
		id: "auto_played",
		payload: { card: 44, player: "ann", tags: ["uni:wild"] }
	};
	const autoOwnCause = {
		id: "auto_played",
		payload: { card: 44, player: "ann", tags: ["uni:wild", "uni:own_log"] }
	};

	it("a normal play posts uni:log.play with name/kind/color params", () => {
		const outputs = presentEvent(
			shippedRegistry(),
			{ sequence: 20, id: "card_played", actor: "ann", payload: { player: "ann", card: 42, tags: [] } },
			VIEWER,
			resolvePlayCard
		);
		const lines = outputs.map((output) => output.line).filter((line) => line !== null);
		expect(lines).toHaveLength(1);
		expect(lines[0]!.key).toBe("uni:log.play");
		expect(lines[0]!.params).toMatchObject({ name: "ann", kind: "uni:number", color: "red" });
	});

	it("an own_log play posts nothing from play.json", () => {
		const keys = lineKeys(shippedRegistry(), {
			sequence: 20, id: "card_played", actor: "ann",
			payload: { player: "ann", card: 42, tags: ["uni:own_log"] }
		});
		expect(keys).toEqual([]);
	});

	it("a wild play + colour change posts uni:log.play via play_wild with the chosen colour and the cause's name/kind", () => {
		const chain = [
			{ sequence: 20, id: "card_played", payload: { card: 44, player: "ann", tags: ["wild", "action", "uni:wild"] } },
			{ sequence: 21, id: "active_type_changed", payload: { color: "blue", previous_color: "white" }, cause: 20 }
		];
		const event = { ...chain[1]!, actor: "ann", causeEvent: wildCause };
		expect(lineKeys(shippedRegistry(), event, recallOf(chain))).toContain("uni:log.play");
		const outputs = presentEvent(shippedRegistry(), event, VIEWER, resolvePlayCard, recallOf(chain) as never);
		const playLine = outputs.map((output) => output.line).find((line) => line?.key === "uni:log.play");
		expect(playLine).toBeDefined();
		expect(playLine!.params).toMatchObject({ name: "ann", kind: "uni:wild", color: "blue" });
	});

	it("an own_log wild play + colour change posts nothing", () => {
		const chain = [
			{ sequence: 20, id: "card_played", payload: { card: 44, player: "ann", tags: ["wild", "action", "uni:wild", "uni:own_log"] } },
			{ sequence: 21, id: "active_type_changed", payload: { color: "blue", previous_color: "white" }, cause: 20 }
		];
		const keys = lineKeys(
			shippedRegistry(),
			{ ...chain[1]!, actor: "ann", causeEvent: wildOwnCause },
			recallOf(chain)
		);
		expect(keys).toEqual([]);
	});

	it("an auto-played wild posts uni:log.auto_play_1 with the chosen colour", () => {
		const chain = [
			{ sequence: 20, id: "auto_played", payload: { card: 44, player: "ann", tags: ["uni:wild"] } },
			{ sequence: 21, id: "active_type_changed", payload: { color: "blue", previous_color: "white" }, cause: 20 }
		];
		const event = { ...chain[1]!, actor: "ann", causeEvent: autoCause };
		expect(lineKeys(shippedRegistry(), event, recallOf(chain))).toContain("uni:log.auto_play_1");
		const outputs = presentEvent(shippedRegistry(), event, VIEWER, resolvePlayCard, recallOf(chain) as never);
		const autoLine = outputs.map((output) => output.line).find((line) => line?.key === "uni:log.auto_play_1");
		expect(autoLine).toBeDefined();
		expect(autoLine!.params).toMatchObject({ name: "ann", color: "blue" });
	});

	it("an own_log auto-played wild posts nothing", () => {
		const chain = [
			{ sequence: 20, id: "auto_played", payload: { card: 44, player: "ann", tags: ["uni:wild", "uni:own_log"] } },
			{ sequence: 21, id: "active_type_changed", payload: { color: "blue", previous_color: "white" }, cause: 20 }
		];
		const keys = lineKeys(
			shippedRegistry(),
			{ ...chain[1]!, actor: "ann", causeEvent: autoOwnCause },
			recallOf(chain)
		);
		expect(keys).toEqual([]);
	});

	it("a wild colour change posts the uni:log.wild_reaction line for run 1, 2 and 3", () => {
		for (const run of [1, 2, 3]) {
			const chain = [
				{ sequence: 20, id: "card_played", payload: { card: 44, player: "ann", tags: ["wild", "action", "uni:wild"] } },
				{ sequence: 21, id: "active_type_changed", payload: { color: "blue", previous_color: "white" }, cause: 20 }
			];
			const keys = lineKeys(
				shippedRegistry(),
				{
					...chain[1]!, actor: "ann", causeEvent: wildCause,
					context: { "uni:wild_run": run, "uni:last_wild_color": { last: "blue", previous: "red" } }
				},
				recallOf(chain)
			);
			expect(keys.some((key) => key.startsWith("uni:log.wild_reaction"))).toBe(true);
		}
	});

	it("an auto-played wild colour change posts the same reaction lines", () => {
		for (const run of [1, 2, 3]) {
			const chain = [
				{ sequence: 20, id: "auto_played", payload: { card: 44, player: "ann", tags: ["uni:wild"] } },
				{ sequence: 21, id: "active_type_changed", payload: { color: "blue", previous_color: "white" }, cause: 20 }
			];
			const keys = lineKeys(
				shippedRegistry(),
				{
					...chain[1]!, actor: "ann", causeEvent: autoCause,
					context: { "uni:wild_run": run, "uni:last_wild_color": { last: "blue", previous: "red" } }
				},
				recallOf(chain)
			);
			expect(keys.some((key) => key.startsWith("uni:log.wild_reaction"))).toBe(true);
		}
	});

	it("an own_log wild colour change posts nothing", () => {
		const cardChain = [
			{ sequence: 20, id: "card_played", payload: { card: 44, player: "ann", tags: ["wild", "action", "uni:wild", "uni:own_log"] } },
			{ sequence: 21, id: "active_type_changed", payload: { color: "blue", previous_color: "white" }, cause: 20 }
		];
		expect(
			lineKeys(
				shippedRegistry(),
				{
					...cardChain[1]!, actor: "ann", causeEvent: wildOwnCause,
					context: { "uni:wild_run": 1, "uni:last_wild_color": { last: "blue", previous: "red" } }
				},
				recallOf(cardChain)
			)
		).toEqual([]);
		const autoChain = [
			{ sequence: 20, id: "auto_played", payload: { card: 44, player: "ann", tags: ["uni:wild", "uni:own_log"] } },
			{ sequence: 21, id: "active_type_changed", payload: { color: "blue", previous_color: "white" }, cause: 20 }
		];
		expect(
			lineKeys(
				shippedRegistry(),
				{
					...autoChain[1]!, actor: "ann", causeEvent: autoOwnCause,
					context: { "uni:wild_run": 1, "uni:last_wild_color": { last: "blue", previous: "red" } }
				},
				recallOf(autoChain)
			)
		).toEqual([]);
	});
});

function readDirShipped(): string[] {
	return shippedFiles()
		.map((file) => (file as { "uni:binding"?: { "uni:description"?: { "uni:identifier"?: string } } })
			["uni:binding"]?.["uni:description"]?.["uni:identifier"] ?? "")
		.filter((id) => id !== "");
}
