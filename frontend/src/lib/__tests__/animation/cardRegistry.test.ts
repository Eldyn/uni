import { describe, it, expect, vi } from "vitest";
import { flushSync } from "svelte";
import { createCardRegistry, CardRegistry } from "$components/game/animation/cardRegistry.svelte";
import type { AnimationBeat } from "$components/game/animation/types";

/** Test helper: mirrors the transitional wrapper baseBeats.svelte.ts uses
 *  around its own anchors objects (Task A7 Step 5) — turns a plain
 *  Record into the resolver function enqueue now requires. */
function resolverFor(anchors: Record<string, [number, number, number]>): (name: string) => [number, number, number] {
	return (name) => {
		const anchor = anchors[name];
		if (!anchor) throw new Error(`no anchor registered for "${name}"`);
		return anchor;
	};
}

describe("CardRegistry", () => {
	it("plays beats in order and fires each step's onLand via enqueue's return", async () => {
		const queue = createCardRegistry();
		const order: string[] = [];

		queue.registerCardMeta("card-1", { type: "red", value: "5" });
		queue.registerCardMeta("card-2", { type: "blue", value: "7" });

		const done = queue.enqueue(
			[
				[{ op: "move", target: "card-1", payload: { to: "discard-pile" } }],
				[{ op: "move", target: "card-2", payload: { to: "discard-pile" } }]
			],
			resolverFor({ "discard-pile": [1, 0, 1] })
		);
		queue.onBeatComplete = (index) => order.push(`beat-${index}`);

		queue.skipCurrent();
		await Promise.resolve();
		queue.skipCurrent();
		await done;

		expect(order).toEqual(["beat-0", "beat-1"]);
	});

	it("unknown op logs a warning and still advances the queue", async () => {
		const queue = createCardRegistry();
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		queue.registerCardMeta("card-1", { type: "red", value: "5" });

		const done = queue.enqueue([[{ op: "not-a-real-op", target: "card-1", payload: {} }]], resolverFor({}));
		await done;

		expect(warn).toHaveBeenCalledWith(expect.stringContaining("not-a-real-op"));
		warn.mockRestore();
	});

	it("skipping still resolves the beat instead of hanging", async () => {
		const queue = createCardRegistry();
		queue.registerCardMeta("card-1", { type: "red", value: "5" });
		const done = queue.enqueue(
			[[{ op: "move", target: "card-1", payload: { to: "discard-pile" } }]],
			resolverFor({ "discard-pile": [1, 0, 1] })
		);
		queue.skipCurrent();
		await expect(done).resolves.toBeUndefined();
	});

	it("a card seeded via seedPose before enqueue still gets a rendered flight", async () => {
		const queue = createCardRegistry();
		queue.registerCardMeta("card-1", { type: "red", value: "5" });

		// baseBeats.svelte.ts calls seedPose (populating #poses) before enqueue
		// ever runs — getPose's activeFlights push must not be gated on #poses.
		queue.seedPose("card-1", {
			x: 1,
			y: 0,
			z: 2,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1
		});

		const done = queue.enqueue(
			[[{ op: "move", target: "card-1", payload: { to: "discard-pile" } }]],
			resolverFor({ "discard-pile": [5, 0, 5] })
		);

		expect(queue.activeFlights).toHaveLength(1);
		expect(queue.activeFlights[0].id).toBe("card-1");
		expect(queue.activeFlights[0].pose.x).toBe(1);

		queue.skipCurrent();
		await done;
	});

	it("seedPose mutates an existing pose in place instead of orphaning an activeFlights entry that already points at it", async () => {
		const queue = createCardRegistry();

		// Mirrors the real race: an owner's registration effect (LocalHand3D's
		// ensureEntry, Task A10) can run BEFORE baseBeats.svelte.ts's seedPose
		// call for the same newly-drawn card id — ensureEntry creates the pose
		// object and pushes it into activeFlights first.
		const idlePose = queue.ensureEntry(
			"card-1",
			{ x: 0, y: 0.04, z: 5, spinDeg: 0, flipDeg: 0, scale: 1, turned: false, opacity: 1 },
			{ type: "red", value: "5" }
		);

		// seedPose then runs — it must update the SAME object activeFlights
		// already references, not swap in a new one the flight never sees.
		queue.seedPose("card-1", {
			x: -3,
			y: 0.64,
			z: 4.94,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: true,
			opacity: 1
		});

		expect(queue.activeFlights).toHaveLength(1);
		const flight = queue.activeFlights[0];
		expect(flight.pose).toBe(idlePose); // same object identity, not replaced
		expect(flight.pose.x).toBe(-3);
		expect(flight.pose.y).toBeCloseTo(0.64);
		expect(flight.pose.z).toBeCloseTo(4.94);
		expect(flight.pose.turned).toBe(true);
	});

	it("the seeded pose object is the same reactive instance GSAP tweens, so the mounted flight updates live", async () => {
		const queue = createCardRegistry();
		queue.registerCardMeta("card-1", { type: "red", value: "5" });
		queue.seedPose("card-1", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1
		});

		const done = queue.enqueue(
			[[{ op: "move", target: "card-1", payload: { to: "discard-pile" } }]],
			resolverFor({ "discard-pile": [9, 0, 9] })
		);

		const flightPose = queue.activeFlights[0].pose;
		// GSAP tweens the object returned by getPose in-place; that must be the
		// exact same object identity handed to the mounted flight, not a copy.
		flightPose.x = 42;
		expect(queue.activeFlights[0].pose.x).toBe(42);

		queue.skipCurrent();
		await done;
	});

	it("a renderer throwing on a bad step doesn't wedge the queue — other steps and later beats still run", async () => {
		const queue = createCardRegistry();
		const error = vi.spyOn(console, "error").mockImplementation(() => {});
		queue.registerCardMeta("card-1", { type: "red", value: "5" });
		queue.registerCardMeta("card-2", { type: "blue", value: "7" });

		const done = queue.enqueue(
			[
				// move step missing payload.to — moveRenderer throws synchronously.
				[{ op: "move", target: "card-1", payload: {} }],
				[{ op: "move", target: "card-2", payload: { to: "discard-pile" } }]
			],
			resolverFor({ "discard-pile": [1, 0, 1] })
		);

		queue.skipCurrent();
		await Promise.resolve();
		queue.skipCurrent();
		await expect(done).resolves.toBeUndefined();

		expect(error).toHaveBeenCalled();
		error.mockRestore();
	});

	it("an unresolvable anchor thrown from a renderer doesn't wedge the queue", async () => {
		const queue = createCardRegistry();
		const error = vi.spyOn(console, "error").mockImplementation(() => {});
		queue.registerCardMeta("card-1", { type: "red", value: "5" });

		const done = queue.enqueue(
			[[{ op: "move", target: "card-1", payload: { to: "unregistered-anchor" } }]],
			resolverFor({})
		);

		await expect(done).resolves.toBeUndefined();
		expect(error).toHaveBeenCalled();
		error.mockRestore();
	});
});

describe("CardRegistry pose providers", () => {
	it("keeps a real entry idle at its provider's pose instead of deleting it when a beat retires it", () => {
		const registry = new CardRegistry();
		registry.registerCardMeta("42", { type: "red", value: "5" });
		registry.seedPose("42", { x: 0, y: 0, z: 0, spinDeg: 0, flipDeg: 0, scale: 1, turned: false, opacity: 1 });
		registry.setPoseProvider("42", () => [9, 0.02, 3]);

		const beat: AnimationBeat = [{ op: "move", target: "42", payload: { to: "somewhere" } }];
		const resolveAnchor = () => [9, 0, 3] as [number, number, number];

		const done = registry.enqueue([beat], resolveAnchor);
		flushSync();
		// GSAP timelines run on rAF; force-complete via skipCurrent for a
		// synchronous, deterministic test (same pattern skip-related tests
		// elsewhere in this codebase already use).
		registry.skipCurrent();
		return done.then(() => {
			expect(registry.activeFlights.some((f) => f.id === "42")).toBe(true);
			const flight = registry.activeFlights.find((f) => f.id === "42")!;
			expect(flight.pose.x).toBeCloseTo(9);
			expect(flight.pose.y).toBeCloseTo(0.02);
			expect(flight.pose.z).toBeCloseTo(3);
		});
	});

	it("deletes an anonymous entry (no provider registered) on retire, as before", () => {
		const registry = new CardRegistry();
		registry.seedPose("draw:bob:0", { x: 0, y: 0, z: 0, spinDeg: 0, flipDeg: 0, scale: 1, turned: true, opacity: 1 });

		const beat: AnimationBeat = [{ op: "move", target: "draw:bob:0", payload: { to: "seat:bob" } }];
		const resolveAnchor = () => [1, 0, -2] as [number, number, number];

		const done = registry.enqueue([beat], resolveAnchor);
		flushSync();
		registry.skipCurrent();
		return done.then(() => {
			expect(registry.activeFlights.some((f) => f.id === "draw:bob:0")).toBe(false);
		});
	});

	it("isInTransit reflects whether an entry currently has a live pose entry from an unfinished beat", () => {
		const registry = new CardRegistry();
		registry.seedPose("7", { x: 0, y: 0, z: 0, spinDeg: 0, flipDeg: 0, scale: 1, turned: false, opacity: 1 });
		expect(registry.isInTransit("7")).toBe(false);

		const beat: AnimationBeat = [{ op: "move", target: "7", payload: { to: "x" } }];
		registry.enqueue([beat], () => [1, 0, 1]);
		flushSync();
		expect(registry.isInTransit("7")).toBe(true);
	});
});
