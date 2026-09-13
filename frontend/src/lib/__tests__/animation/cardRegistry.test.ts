import { describe, it, expect, vi } from "vitest";
import { createCardRegistry } from "$components/game/animation/cardRegistry.svelte";

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
			{ "discard-pile": [1, 0, 1] }
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

		const done = queue.enqueue([[{ op: "not-a-real-op", target: "card-1", payload: {} }]], {});
		await done;

		expect(warn).toHaveBeenCalledWith(expect.stringContaining("not-a-real-op"));
		warn.mockRestore();
	});

	it("skipping still resolves the beat instead of hanging", async () => {
		const queue = createCardRegistry();
		queue.registerCardMeta("card-1", { type: "red", value: "5" });
		const done = queue.enqueue(
			[[{ op: "move", target: "card-1", payload: { to: "discard-pile" } }]],
			{ "discard-pile": [1, 0, 1] }
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
			scale: 1,
			turned: false,
			opacity: 1
		});

		const done = queue.enqueue(
			[[{ op: "move", target: "card-1", payload: { to: "discard-pile" } }]],
			{ "discard-pile": [5, 0, 5] }
		);

		expect(queue.activeFlights).toHaveLength(1);
		expect(queue.activeFlights[0].id).toBe("card-1");
		expect(queue.activeFlights[0].pose.x).toBe(1);

		queue.skipCurrent();
		await done;
	});

	it("the seeded pose object is the same reactive instance GSAP tweens, so the mounted flight updates live", async () => {
		const queue = createCardRegistry();
		queue.registerCardMeta("card-1", { type: "red", value: "5" });
		queue.seedPose("card-1", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1
		});

		const done = queue.enqueue(
			[[{ op: "move", target: "card-1", payload: { to: "discard-pile" } }]],
			{ "discard-pile": [9, 0, 9] }
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
			{ "discard-pile": [1, 0, 1] }
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
			{}
		);

		await expect(done).resolves.toBeUndefined();
		expect(error).toHaveBeenCalled();
		error.mockRestore();
	});
});
