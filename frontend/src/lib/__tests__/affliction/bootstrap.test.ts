import { describe, it, expect, vi } from "vitest";
import { AfflictionStore } from "$stores/affliction.svelte";
import { EntityRuntime } from "$components/game/entity/entityRuntime.svelte";
import type { MatchEventSubscriber } from "$components/game/affliction/triggers";

/** A controllable stand-in for `storeGame.onMatchEvent`. */
function fakeFeed(): {
	subscribe: MatchEventSubscriber;
	emit: (type: string, payload: unknown) => void;
} {
	let handler: ((type: string, payload: unknown) => void) | null = null;
	return {
		subscribe: (cb) => {
			handler = cb;
			return () => {
				handler = null;
			};
		},
		emit: (type, payload) => handler?.(type, payload)
	};
}

function defsPayload() {
	return {
		entities: [
			{
				id: "test:dice",
				def_hash: "abc",
				components: {
					"outcome.resolver": { "test:six": { animation: "land_6" }, "*": {} },
					"lifetime.ttl": { seconds: 3 }
				}
			}
		],
		presentations: [
			{
				id: "test:lsd",
				target: "card:42",
				priority: 10,
				modifiers: [{ kind: "face.override", texture: "bomb" }]
			}
		]
	};
}

describe("AfflictionStore: defs bootstrap (ruling 2)", () => {
	it("consumes defs.entities and defines them in the shared runtime", () => {
		const store = new AfflictionStore();
		const runtime = new EntityRuntime();
		const feed = fakeFeed();
		store.bind(runtime, feed.subscribe);

		feed.emit("defs", defsPayload());

		expect(runtime.spawn("test:dice")).toBeGreaterThan(0);
		expect(runtime.liveCount).toBe(1);
	});

	it("registers always-on presentations and exposes their face override", () => {
		const store = new AfflictionStore();
		const feed = fakeFeed();
		store.bind(new EntityRuntime(), feed.subscribe);

		expect(store.override("42")).toBeUndefined();
		feed.emit("defs", defsPayload());
		expect(store.override("42")).toEqual({ texture: "bomb" });
	});

	it("installs match-event presentations when their where matches", () => {
		const store = new AfflictionStore();
		const feed = fakeFeed();
		store.bind(new EntityRuntime(), feed.subscribe);

		feed.emit("defs", {
			presentations: [
				{
					id: "test:on-play",
					target: "card:9",
					modifiers: [{ kind: "face.override", texture: "hit" }],
					triggers: [{ on: "card_played", where: { player: "a" } }]
				}
			]
		});

		feed.emit("card_played", { player: "b" });
		expect(store.override("9")).toBeUndefined();
		feed.emit("card_played", { player: "a" });
		expect(store.override("9")).toEqual({ texture: "hit" });
	});

	it("honours the enabled gate (reduced-motion / animation off)", () => {
		const store = new AfflictionStore();
		const feed = fakeFeed();
		store.bind(new EntityRuntime(), feed.subscribe);

		store.enabled = false;
		feed.emit("defs", defsPayload());
		expect(store.override("42")).toBeUndefined();

		store.enabled = true;
		feed.emit("defs", defsPayload());
		expect(store.override("42")).toEqual({ texture: "bomb" });
	});

	it("resolves a `seeded-random-tile` source from the per-client cosmetic RNG", () => {
		const store = new AfflictionStore();
		const feed = fakeFeed();
		store.bind(new EntityRuntime(), feed.subscribe);

		feed.emit("defs", {
			presentations: [
				{
					id: "test:lsd",
					target: "card:7",
					modifiers: [{ kind: "face.override", source: "seeded-random-tile" }]
				}
			]
		});

		const override = store.override("7");
		expect(override?.texture).toBeTypeOf("string");
		expect((override?.texture ?? "").length).toBeGreaterThan(0);
	});

	it("degrades a malformed defs payload without throwing", () => {
		const store = new AfflictionStore();
		const feed = fakeFeed();
		store.bind(new EntityRuntime(), feed.subscribe);

		expect(() => feed.emit("defs", null)).not.toThrow();
		expect(() => feed.emit("defs", { entities: "nope", presentations: [42] })).not.toThrow();
	});

	it("dispose detaches the subscription and cancels timers", () => {
		vi.useFakeTimers();
		try {
			const store = new AfflictionStore();
			const feed = fakeFeed();
			const dispose = store.bind(new EntityRuntime(), feed.subscribe);

			feed.emit("defs", {
				presentations: [
					{
						id: "test:timed",
						target: "card:3",
						modifiers: [{ kind: "face.override", texture: "temp" }],
						triggers: [{ duration: 5 }]
					}
				]
			});
			expect(store.override("3")).toEqual({ texture: "temp" });

			dispose();
			expect(() => feed.emit("defs", defsPayload())).not.toThrow();
			expect(store.override("42")).toBeUndefined();
		} finally {
			vi.useRealTimers();
		}
	});
});
