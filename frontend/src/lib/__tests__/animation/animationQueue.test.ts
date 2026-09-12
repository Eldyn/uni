import { describe, it, expect, vi } from "vitest";
import { createAnimationQueue } from "$components/game/animation/animationQueue.svelte";

describe("AnimationQueue", () => {
	it("plays beats in order and fires each step's onLand via enqueue's return", async () => {
		const queue = createAnimationQueue();
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
		const queue = createAnimationQueue();
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		queue.registerCardMeta("card-1", { type: "red", value: "5" });

		const done = queue.enqueue([[{ op: "not-a-real-op", target: "card-1", payload: {} }]], {});
		await done;

		expect(warn).toHaveBeenCalledWith(expect.stringContaining("not-a-real-op"));
		warn.mockRestore();
	});

	it("skipping still resolves the beat instead of hanging", async () => {
		const queue = createAnimationQueue();
		queue.registerCardMeta("card-1", { type: "red", value: "5" });
		const done = queue.enqueue(
			[[{ op: "move", target: "card-1", payload: { to: "discard-pile" } }]],
			{ "discard-pile": [1, 0, 1] }
		);
		queue.skipCurrent();
		await expect(done).resolves.toBeUndefined();
	});
});
