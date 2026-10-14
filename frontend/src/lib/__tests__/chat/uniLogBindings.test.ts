import { readFileSync, readdirSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { BindingRegistry, presentEvent, type CardResolver } from "$lib/presentation/eventBindings";

const SHIPPED_DIR = resolve(import.meta.dirname, "../../../../../mods/uni/presentations");
const VIEWER = { seat: "ann" };

const resolveCard: CardResolver = () => undefined;

/**
 * The shipped uni log bindings as the wire carries them: each parsed file
 * with the `id` the server injects at defs-build time (`uni:<stem>`), the
 * way the parity registry does. The on-disk schema forbids the key.
 */
function shippedFiles(): Record<string, unknown>[] {
	return readdirSync(SHIPPED_DIR)
		.filter((file) => /^(skip|reverse|draw|blocked|reshuffle|keep|near_win|elimination|win|play|play_wild|auto_play|auto_play_wild|wild)\.json$/.test(file))
		.map((file) => {
			const stem = file.slice(0, -".json".length);
			return { id: `uni:${stem}`, ...JSON.parse(readFileSync(resolve(SHIPPED_DIR, file), "utf8")) };
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

function readDirShipped(): string[] {
	return shippedFiles()
		.map((file) => (file as { "uni:binding"?: { "uni:description"?: { "uni:identifier"?: string } } })
			["uni:binding"]?.["uni:description"]?.["uni:identifier"] ?? "")
		.filter((id) => id !== "");
}
