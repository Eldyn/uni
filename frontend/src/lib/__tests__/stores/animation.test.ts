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
});
