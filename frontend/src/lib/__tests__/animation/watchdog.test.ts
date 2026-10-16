import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { gsap } from "gsap";
import { createCardRegistry } from "$components/game/animation/cardRegistry.svelte";
import { storeFxBudget } from "$stores/fxBudget.svelte";
import { storeAnimation } from "$stores/animation.svelte";

/** Test helper: mirrors the transitional wrapper baseBeats.svelte.ts uses
 *  around its own anchors objects — turns a plain Record into the resolver
 *  function enqueue requires. */
function resolverFor(
	anchors: Record<string, [number, number, number]>
): (name: string) => [number, number, number] {
	return (name) => {
		const anchor = anchors[name];
		if (!anchor) throw new Error(`no anchor registered for "${name}"`);
		return anchor;
	};
}

/** A blocking timeline that cannot finish within a test: a 60s tween is far
 *  longer than either the watchdog timeout or any fake-timer advance here. */
function stuckTimeline(): gsap.core.Timeline {
	return gsap.timeline().to({}, { duration: 60 });
}

describe("storeFxBudget", () => {
	it("defaults to the documented budget baselines", () => {
		expect(storeFxBudget.maxEntities).toBe(64);
		expect(storeFxBudget.maxAfflictions).toBe(8);
		expect(storeFxBudget.screenStackMax).toBe(3);
		expect(storeFxBudget.stepTimeoutMs).toBe(8000);
		expect(storeFxBudget.queueMax).toBe(64);
	});
});

describe("CardRegistry watchdog", () => {
	beforeEach(() => vi.useFakeTimers());
	afterEach(() => {
		vi.useRealTimers();
		vi.restoreAllMocks();
	});

	it("force-completes a beat whose blocking timeline never completes and advances the queue", async () => {
		const queue = createCardRegistry();
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const skip = vi.fn();
		queue.register("stuck", () => ({ skip, blocking: stuckTimeline(), persistent: false }));
		queue.registerCardMeta("card-1", { type: "red", value: "5" });

		const completed: number[] = [];
		queue.onBeatComplete = (index) => completed.push(index);

		const done = queue.enqueue(
			[
				[{ op: "stuck", target: "A" }],
				[{ op: "move", target: "card-1", payload: { to: "discard-pile" } }]
			],
			resolverFor({ "discard-pile": [1, 0, 1] })
		);

		// The blocking timeline never completes on its own, so nothing has advanced.
		expect(completed).toEqual([]);

		await vi.advanceTimersByTimeAsync(storeFxBudget.stepTimeoutMs);
		expect(completed).toEqual([0]);

		// Beat 1 is a normal flight that either lands naturally or is itself forced.
		await vi.advanceTimersByTimeAsync(storeFxBudget.stepTimeoutMs);
		await expect(done).resolves.toBeUndefined();
		expect(completed).toEqual([0, 1]);

		expect(skip).toHaveBeenCalledTimes(1);
		expect(warn).toHaveBeenCalledWith(expect.stringContaining("watchdog"));
	});

	it("defeats the timeScale(0) permanent wedge", async () => {
		const queue = createCardRegistry();
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		queue.registerCardMeta("card-1", { type: "red", value: "5" });

		const completed: number[] = [];
		queue.onBeatComplete = (index) => completed.push(index);

		const previous = storeAnimation.speedMultiplier;
		storeAnimation.speedMultiplier = 0;
		try {
			const done = queue.enqueue(
				[[{ op: "move", target: "card-1", payload: { to: "discard-pile" } }]],
				resolverFor({ "discard-pile": [1, 0, 1] })
			);

			// timeScale(0) freezes the composite timeline: onComplete never fires.
			expect(completed).toEqual([]);

			await vi.advanceTimersByTimeAsync(storeFxBudget.stepTimeoutMs);
			await expect(done).resolves.toBeUndefined();
			expect(completed).toEqual([0]);
			// Proves the watchdog cleared the wedge, not a natural completion.
			expect(warn).toHaveBeenCalledWith(expect.stringContaining("watchdog"));
		} finally {
			storeAnimation.speedMultiplier = previous;
		}
	});

	it("does not session-disable a healthy op sharing a beat with a non-settling op", async () => {
		const queue = createCardRegistry();
		vi.spyOn(console, "warn").mockImplementation(() => {});
		// A healthy, short blocking op: on its own it would complete normally.
		const healthy = vi.fn(() => gsap.timeline().to({}, { duration: 0.25 }));
		queue.register("healthy", healthy);
		queue.register("stuck", () => ({ skip() {}, blocking: stuckTimeline(), persistent: false }));

		// The composite beat's duration is dominated by the non-settling op, so
		// the whole beat times out — but only `stuck` is at fault.
		const done = queue.enqueue(
			[
				[
					{ op: "healthy", target: "card-1" },
					{ op: "stuck", target: "card-2" }
				]
			],
			resolverFor({})
		);
		await vi.advanceTimersByTimeAsync(storeFxBudget.stepTimeoutMs);
		await expect(done).resolves.toBeUndefined();
		expect(healthy).toHaveBeenCalledTimes(1);

		// The healthy op must still render in a later batch: the watchdog must
		// not have disabled it for the session.
		const later = queue.enqueue([[{ op: "healthy", target: "card-3" }]], resolverFor({}));
		await vi.advanceTimersByTimeAsync(storeFxBudget.stepTimeoutMs);
		await expect(later).resolves.toBeUndefined();
		expect(healthy).toHaveBeenCalledTimes(2);
	});
});

describe("CardRegistry queue cap", () => {
	it("drops the oldest waiting batch and warns when enqueue hits queueMax", async () => {
		const queue = createCardRegistry();
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		queue.register("stuck", () => ({ skip() {}, blocking: stuckTimeline(), persistent: false }));

		// A never-completing head keeps every later batch sitting in #pending.
		const current = queue.enqueue([[{ op: "stuck", target: "A" }]], resolverFor({}));

		const waiting: Promise<void>[] = [];
		for (let i = 0; i < storeFxBudget.queueMax - 1; i++) {
			waiting.push(
				queue.enqueue(
					[[{ op: "move", target: `w${i}`, payload: { to: "x" } }]],
					resolverFor({ x: [1, 0, 1] })
				)
			);
		}

		const oldest = waiting[0];
		let oldestResolved = false;
		oldest.then(() => {
			oldestResolved = true;
		});

		// One batch over the cap: the oldest *waiting* batch must be dropped and
		// resolved so its caller doesn't hang.
		const extra = queue.enqueue(
			[[{ op: "move", target: "extra", payload: { to: "x" } }]],
			resolverFor({ x: [1, 0, 1] })
		);
		await Promise.resolve();

		expect(warn).toHaveBeenCalledWith(expect.stringContaining("cap"));
		expect(oldestResolved).toBe(true);

		queue.flushImmediately();
		await Promise.all([current, ...waiting, extra]);
	});
});
