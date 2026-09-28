import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import { StoreMatRipple } from "$components/game/three/ripple/matRipple.svelte";
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

	it("goes active with the given geometry and 450ms duration for a normal play", () => {
		const store = new StoreMatRipple(() => 0);

		store.startMatRipple("#00ff00", "normal", { u: 0.25, v: 0.75 }, 2.5);

		expect(store.active).toBe(true);
		expect(store.toColor).toBe("#00ff00");
		expect(store.originUv).toEqual({ u: 0.25, v: 0.75 });
		expect(store.maxRadius).toBe(2.5);
		expect(store.durationMs).toBe(450);
	});

	it("commits the target colour and clears active once the duration elapses", () => {
		vi.useFakeTimers();
		const store = new StoreMatRipple(() => 0);

		store.startMatRipple("#0000ff", "normal", { u: 0.5, v: 0.5 }, 1);
		expect(store.active).toBe(true);

		vi.advanceTimersByTime(450);

		expect(store.active).toBe(false);
		expect(store.committedColor).toBe("#0000ff");
	});

	it("finishes an in-flight ripple to its own target before starting the next one", () => {
		vi.useFakeTimers();
		const store = new StoreMatRipple(() => 0);

		store.startMatRipple("#111111", "normal", { u: 0.5, v: 0.5 }, 1);
		vi.advanceTimersByTime(100); // well short of the 450ms duration

		store.startMatRipple("#222222", "normal", { u: 0.1, v: 0.1 }, 1);

		expect(store.fromColor).toBe("#111111");
		expect(store.toColor).toBe("#222222");
		expect(store.active).toBe(true);
	});

	it("ignores syncColor while a ripple is active", () => {
		vi.useFakeTimers();
		const store = new StoreMatRipple(() => 0);
		store.committedColor = "#abcabc";

		store.startMatRipple("#0000ff", "normal", { u: 0.5, v: 0.5 }, 1);
		store.syncColor("#ffffff");

		expect(store.committedColor).not.toBe("#ffffff");

		vi.advanceTimersByTime(450);
		store.syncColor("#ffffff");
		expect(store.committedColor).toBe("#ffffff");
	});
});
