import { describe, it, expect, vi } from "vitest";
import { flushSync } from "svelte";
import { createCardRegistry, CardRegistry } from "$components/game/animation/cardRegistry.svelte";
import type { AnimationBeat } from "$components/game/animation/types";

/** Test helper: mirrors the transitional wrapper baseBeats.svelte.ts uses
 *  around its own anchors objects (Task A7 Step 5) — turns a plain
 *  Record into the resolver function enqueue now requires. */
function resolverFor(
	anchors: Record<string, [number, number, number]>
): (name: string) => [number, number, number] {
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

		const done = queue.enqueue(
			[[{ op: "not-a-real-op", target: "card-1", payload: {} }]],
			resolverFor({})
		);
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
		registry.seedPose("42", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1
		});
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

	it("releaseLanded hands a landed card back to its owner while the rest of its beat plays", () => {
		const registry = new CardRegistry();
		for (const id of ["A", "B"]) {
			registry.registerCardMeta(id, { type: "red", value: "5" });
			registry.seedPose(id, {
				x: 0,
				y: 0,
				z: 0,
				spinDeg: 0,
				flipDeg: 0,
				scale: 1,
				turned: false,
				opacity: 1
			});
		}
		registry.setPoseProvider("A", () => [4, 0.02, 2]);

		const beat: AnimationBeat = [
			{ op: "move", target: "A", payload: { to: "slot" } },
			{ op: "move", target: "B", payload: { to: "slot" }, atS: 0.1 }
		];
		const done = registry.enqueue([beat], () => [9, 0, 3]);
		flushSync();
		expect(registry.isBeatTarget("A")).toBe(true);

		registry.releaseLanded("A");
		expect(registry.isInTransit("A")).toBe(false);
		expect(registry.isBeatTarget("A")).toBe(false);
		expect(registry.isInTransit("B")).toBe(true);
		expect(registry.getPose("A")!.x).toBeCloseTo(4);
		expect(registry.getPose("A")!.z).toBeCloseTo(2);

		registry.skipCurrent();
		return done.then(() => {
			expect(registry.isInTransit("B")).toBe(false);
			expect(registry.isBeatTarget("A")).toBe(false);
			expect(registry.isBeatTarget("B")).toBe(false);
		});
	});

	it("deletes an anonymous entry (no provider registered) on retire, as before", () => {
		const registry = new CardRegistry();
		registry.seedPose("draw:bob:0", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: true,
			opacity: 1
		});

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
		registry.seedPose("7", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1
		});
		expect(registry.isInTransit("7")).toBe(false);

		const beat: AnimationBeat = [{ op: "move", target: "7", payload: { to: "x" } }];
		registry.enqueue([beat], () => [1, 0, 1]);
		flushSync();
		expect(registry.isInTransit("7")).toBe(true);
	});

	it("reserves a queued beat's targets up front, so an owner can't delete the seeded pose while it waits", async () => {
		const registry = new CardRegistry();
		const seed = (id: string) =>
			registry.seedPose(id, {
				x: 0,
				y: 0,
				z: 0,
				spinDeg: 0,
				flipDeg: 0,
				scale: 1,
				turned: false,
				opacity: 1
			});
		seed("A");
		seed("B");

		// A starts playing immediately; B is queued behind it and hasn't begun.
		const doneA = registry.enqueue([[{ op: "move", target: "A", payload: { to: "x" } }]], () => [
			1, 0, 1
		]);
		const doneB = registry.enqueue([[{ op: "move", target: "B", payload: { to: "y" } }]], () => [
			2, 0, 2
		]);

		// The regression: without the up-front reservation, B was only marked
		// in-transit when its own beat started, leaving a window in which the
		// hand's cleanup effect saw it leave the row, removed it, and the
		// flight then recreated it at the origin as the dummy white "0".
		expect(registry.isInTransit("B")).toBe(true);
		expect(registry.getPose("B")).toBeDefined();

		registry.skipCurrent(); // finish A, pump B
		registry.skipCurrent(); // finish B
		await doneA;
		await doneB;
		flushSync();
		expect(registry.isInTransit("B")).toBe(false);
	});

	it("isInTransit is reactive and triggers effects when transit state changes", async () => {
		const registry = new CardRegistry();
		registry.seedPose("7", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1
		});

		let observed = false;
		let runs = 0;
		const cleanup = $effect.root(() => {
			$effect(() => {
				runs++;
				observed = registry.isInTransit("7");
			});
		});
		flushSync();
		expect(runs).toBe(1);
		expect(observed).toBe(false);

		const done = registry.enqueue([[{ op: "move", target: "7", payload: { to: "x" } }]], () => [
			1, 0, 1
		]);
		flushSync();
		expect(runs).toBe(2);
		expect(observed).toBe(true);

		registry.skipCurrent();
		await done;
		flushSync();
		expect(runs).toBe(3);
		expect(observed).toBe(false);

		cleanup();
	});

	it("setDecoration updates decoration in-place without replacing activeFlights array reference", () => {
		const registry = new CardRegistry();
		registry.ensureEntry(
			"c1",
			{ x: 0, y: 0, z: 0, spinDeg: 0, flipDeg: 0, scale: 1, turned: false, opacity: 1 },
			{ type: "red", value: "5" }
		);
		const originalArray = registry.activeFlights;
		registry.setDecoration("c1", { hovered: true });
		expect(registry.activeFlights).toBe(originalArray);
		expect(registry.activeFlights[0].decoration?.hovered).toBe(true);
	});

	it("ensureEntry and setDecoration called inside an effect do not cause a reactive cycle", () => {
		const registry = new CardRegistry();
		let runCount = 0;
		const cleanup = $effect.root(() => {
			$effect(() => {
				runCount++;
				registry.ensureEntry(
					"c1",
					{ x: 0, y: 0, z: 0, spinDeg: 0, flipDeg: 0, scale: 1, turned: false, opacity: 1 },
					{ type: "red", value: "5" }
				);
				registry.applyIdlePoseIfNotInTransit("c1");
				registry.setDecoration("c1", { hovered: true });
			});
		});
		flushSync();
		expect(runCount).toBe(1);
		cleanup();
	});

	it("clearDecoration deletes decoration outright instead of merging", () => {
		const registry = new CardRegistry();
		registry.ensureEntry(
			"42",
			{ x: 0, y: 0, z: 0, spinDeg: 0, flipDeg: 0, scale: 1, turned: false, opacity: 1 },
			{ type: "red", value: "5" }
		);
		registry.setDecoration("42", { hovered: true, hoverSpinDeg: 9, pushX: 1.5 });

		const handleBefore = registry.activeFlights.find((f) => f.id === "42");
		expect(handleBefore?.decoration?.hoverSpinDeg).toBe(9);

		registry.clearDecoration("42");
		const handleAfter = registry.activeFlights.find((f) => f.id === "42");
		expect(handleAfter?.decoration).toBeUndefined();

		// Subsequent setDecoration starts from empty, not merging previous hoverSpinDeg
		registry.setDecoration("42", { pushX: 2 });
		expect(handleAfter?.decoration?.pushX).toBe(2);
		expect(handleAfter?.decoration?.hoverSpinDeg).toBeUndefined();
	});
});
