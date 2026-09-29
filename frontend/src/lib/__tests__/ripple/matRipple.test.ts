import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import { StoreMatRipple } from "$components/game/three/ripple/matRipple.svelte";
import { rippleDurationMs } from "$components/game/three/ripple/ripplePlan";
import { storeRenderSettings } from "$stores/renderSettings.svelte";

describe("StoreMatRipple", () => {
	beforeEach(() => {
		storeRenderSettings.matRipple = true;
	});

	afterEach(() => {
		storeRenderSettings.matRipple = true;
		vi.useRealTimers();
	});

	it("commits the colour instantly and stays inactive when matRippleActive is off", () => {
		storeRenderSettings.matRipple = false;
		const store = new StoreMatRipple();

		store.startMatRipple("#ff0000", "normal", { u: 0.5, v: 0.5 }, 1);

		expect(store.committedColor).toBe("#ff0000");
		expect(store.active).toBe(false);
	});

	it("uses the injected now() as the ripple's start time", () => {
		const now = vi.fn(() => 12345);
		const store = new StoreMatRipple(now);

		store.startMatRipple("#ff0000", "normal", { u: 0.5, v: 0.5 }, 1);

		expect(store.startTimeMs).toBe(12345);
	});

	it("goes active with the given geometry and the step-aligned normal duration", () => {
		const store = new StoreMatRipple(() => 0);

		store.startMatRipple("#00ff00", "normal", { u: 0.25, v: 0.75 }, 2.5);

		expect(store.active).toBe(true);
		expect(store.toColor).toBe("#00ff00");
		expect(store.originUv).toEqual({ u: 0.25, v: 0.75 });
		expect(store.maxRadius).toBe(2.5);
		expect(store.durationMs).toBe(rippleDurationMs("normal", 1));
	});

	it("goes active with the step-aligned wild duration for a wild play", () => {
		const store = new StoreMatRipple(() => 0);

		store.startMatRipple("#ff00ff", "wild", { u: 0.5, v: 0.5 }, 1);

		expect(store.active).toBe(true);
		expect(store.strength).toBe("wild");
		expect(store.durationMs).toBe(rippleDurationMs("wild", 1));
	});

	it("commits the target colour and clears active once the duration elapses", () => {
		vi.useFakeTimers();
		const store = new StoreMatRipple(() => 0);

		store.startMatRipple("#0000ff", "normal", { u: 0.5, v: 0.5 }, 1);
		expect(store.active).toBe(true);

		vi.advanceTimersByTime(store.durationMs);

		expect(store.active).toBe(false);
		expect(store.committedColor).toBe("#0000ff");
	});

	it("is still active 1ms before the aligned duration completes", () => {
		vi.useFakeTimers();
		const store = new StoreMatRipple(() => 0);

		store.startMatRipple("#0000ff", "normal", { u: 0.5, v: 0.5 }, 1);
		vi.advanceTimersByTime(store.durationMs - 1);

		expect(store.active).toBe(true);
		expect(store.committedColor).not.toBe("#0000ff");
	});

	it("finishes an in-flight ripple to its own target before starting the next one", () => {
		vi.useFakeTimers();
		const store = new StoreMatRipple(() => 0);

		store.startMatRipple("#111111", "normal", { u: 0.5, v: 0.5 }, 1);
		vi.advanceTimersByTime(100); // well short of the aligned duration

		store.startMatRipple("#222222", "normal", { u: 0.1, v: 0.1 }, 1);

		expect(store.fromColor).toBe("#111111");
		expect(store.toColor).toBe("#222222");
		expect(store.active).toBe(true);
	});

	it("clears the running timer and commits instantly if ripples turn off mid-flight", () => {
		vi.useFakeTimers();
		const store = new StoreMatRipple(() => 0);

		store.startMatRipple("#111111", "normal", { u: 0.5, v: 0.5 }, 1);
		expect(store.active).toBe(true);

		storeRenderSettings.matRipple = false;
		store.startMatRipple("#333333", "normal", { u: 0.2, v: 0.2 }, 1);

		expect(store.active).toBe(false);
		expect(store.committedColor).toBe("#333333");

		// The old timer must actually be cleared, not just superseded: advancing
		// past where it would have fired must not flip active back on or stomp
		// the instantly-committed colour.
		vi.advanceTimersByTime(10_000);
		expect(store.active).toBe(false);
		expect(store.committedColor).toBe("#333333");
	});

	it("ignores syncColor while a ripple is active", () => {
		vi.useFakeTimers();
		const store = new StoreMatRipple(() => 0);
		store.committedColor = "#abcabc";

		store.startMatRipple("#0000ff", "normal", { u: 0.5, v: 0.5 }, 1);
		store.syncColor("#ffffff");

		expect(store.committedColor).not.toBe("#ffffff");

		vi.advanceTimersByTime(store.durationMs);
		store.syncColor("#ffffff");
		expect(store.committedColor).toBe("#ffffff");
	});

	it("beginPending holds the committed colour and suppresses syncColor until the landing", () => {
		vi.useFakeTimers();
		const store = new StoreMatRipple(() => 0);
		store.committedColor = "#bd3130";

		store.beginPending();
		store.syncColor("#0470dd");

		expect(store.pending).toBe(true);
		expect(store.committedColor).toBe("#bd3130");

		store.startMatRipple("#0470dd", "normal", { u: 0.5, v: 0.5 }, 1);

		expect(store.fromColor).toBe("#bd3130");
		expect(store.toColor).toBe("#0470dd");
		expect(store.pending).toBe(false);

		vi.advanceTimersByTime(store.durationMs);
		expect(store.committedColor).toBe("#0470dd");
	});

	it("beginPending is a no-op when ripples are inactive, so the colour still commits instantly", () => {
		storeRenderSettings.matRipple = false;
		const store = new StoreMatRipple(() => 0);
		store.committedColor = "#bd3130";

		store.beginPending();
		store.syncColor("#0470dd");

		expect(store.pending).toBe(false);
		expect(store.committedColor).toBe("#0470dd");
	});

	it("clearPending drops the hold so a later syncColor adopts the new colour", () => {
		const store = new StoreMatRipple(() => 0);
		store.committedColor = "#bd3130";

		store.beginPending();
		store.clearPending();
		store.syncColor("#0470dd");

		expect(store.pending).toBe(false);
		expect(store.committedColor).toBe("#0470dd");
	});

	it("beginPending captures a running sweep's just-committed target, not its stale from-colour", () => {
		vi.useFakeTimers();
		const store = new StoreMatRipple(() => 0);
		store.committedColor = "#bd3130";

		// Sweep A is still running (red -> blue): committedColor is still red.
		store.startMatRipple("#0470dd", "normal", { u: 0.5, v: 0.5 }, 1);
		expect(store.active).toBe(true);

		// A second play arrives mid-sweep, then its card lands.
		store.beginPending();
		store.startMatRipple("#00aa00", "normal", { u: 0.5, v: 0.5 }, 1);

		expect(store.fromColor).toBe("#0470dd");
		expect(store.toColor).toBe("#00aa00");
	});

	it("startMatRipple without a pending hold still sweeps from the committed colour", () => {
		const store = new StoreMatRipple(() => 0);
		store.committedColor = "#bd3130";

		store.startMatRipple("#0470dd", "normal", { u: 0.5, v: 0.5 }, 1);

		expect(store.fromColor).toBe("#bd3130");
		expect(store.toColor).toBe("#0470dd");
	});

	it("reset drops a running sweep and a pending hold so syncColor applies again", () => {
		vi.useFakeTimers();
		const store = new StoreMatRipple(() => 0);
		store.startMatRipple("#ff0000", "normal", { u: 0.5, v: 0.5 }, 1);
		store.beginPending();

		store.reset();
		store.syncColor("#00ff00");

		expect(store.active).toBe(false);
		expect(store.pending).toBe(false);
		expect(store.committedColor).toBe("#00ff00");
		expect(vi.getTimerCount()).toBe(0);
	});
});
