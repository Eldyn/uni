import { describe, it, expect, beforeEach, vi } from "vitest";

describe("storeAnimation", () => {
	beforeEach(() => {
		localStorage.clear();
		vi.resetModules();
	});

	it("defaults enabled to the inverse of reduced-motion, and speedMultiplier to 1", async () => {
		const { storeAnimation } = await import("$stores/animation.svelte");
		expect(storeAnimation.speedMultiplier).toBe(1);
		// storeWebglCapability.reducedMotion defaults false under jsdom's default
		// matchMedia mock (see setup.ts) — enabled should therefore default true.
		expect(storeAnimation.enabled).toBe(true);
	});

	it("persists a changed speedMultiplier across store re-creation", async () => {
		const { storeAnimation } = await import("$stores/animation.svelte");
		storeAnimation.setSpeedMultiplier(2);
		expect(JSON.parse(localStorage.getItem("uni:animation:settings")!).speedMultiplier).toBe(2);
	});

	it("setEnabled(false) persists and reads back false", async () => {
		const { storeAnimation } = await import("$stores/animation.svelte");
		storeAnimation.setEnabled(false);
		expect(JSON.parse(localStorage.getItem("uni:animation:settings")!).enabled).toBe(false);
	});

	it("setSpeedMultiplier clamps out-of-range and non-finite values instead of wedging the queue", async () => {
		const { storeAnimation } = await import("$stores/animation.svelte");

		storeAnimation.setSpeedMultiplier(0);
		expect(storeAnimation.speedMultiplier).toBeGreaterThanOrEqual(0.5);

		storeAnimation.setSpeedMultiplier(-5);
		expect(storeAnimation.speedMultiplier).toBeGreaterThanOrEqual(0.5);

		storeAnimation.setSpeedMultiplier(NaN);
		expect(Number.isFinite(storeAnimation.speedMultiplier)).toBe(true);

		storeAnimation.setSpeedMultiplier(999);
		expect(storeAnimation.speedMultiplier).toBeLessThanOrEqual(3);
	});

	it("rejects an unvalidated speedMultiplier written directly to localStorage (user-writable), instead of loading 0", async () => {
		localStorage.setItem("uni:animation:settings", JSON.stringify({ speedMultiplier: 0 }));
		const { storeAnimation } = await import("$stores/animation.svelte");
		expect(storeAnimation.speedMultiplier).toBeGreaterThanOrEqual(0.5);
	});

	it("defaults alwaysUprightValues to false (opt-in, not forced)", async () => {
		const { storeAnimation } = await import("$stores/animation.svelte");
		expect(storeAnimation.alwaysUprightValues).toBe(false);
	});

	it("persists alwaysUprightValues across the same pattern as setEnabled/setSpeedMultiplier", async () => {
		const { storeAnimation } = await import("$stores/animation.svelte");
		storeAnimation.setAlwaysUprightValues(true);
		expect(storeAnimation.alwaysUprightValues).toBe(true);
		const raw = localStorage.getItem("uni:animation:settings");
		expect(raw).toBeTruthy();
		expect(JSON.parse(raw!).alwaysUprightValues).toBe(true);
	});

	it("restores alwaysUprightValues from localStorage if present", async () => {
		localStorage.setItem("uni:animation:settings", JSON.stringify({ alwaysUprightValues: true }));
		const { storeAnimation } = await import("$stores/animation.svelte");
		expect(storeAnimation.alwaysUprightValues).toBe(true);
	});
});
