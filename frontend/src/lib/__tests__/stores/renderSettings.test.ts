import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import { RenderSettings, SETTINGS_STORAGE_KEY } from "$stores/renderSettings.svelte";
import { storeWebglCapability } from "$stores/webglCapability.svelte";
import { storeAnimation } from "$stores/animation.svelte";

describe("RenderSettings store", () => {
	beforeEach(() => {
		localStorage.clear();
		vi.restoreAllMocks();
	});

	it("initializes with default values", () => {
		const store = new RenderSettings();
		expect(store.cardRenderMode).toBe("atlas");
		expect(["full", "capped"]).toContain(store.drawPileThickness);
		expect(store.clickToPlay).toBe(true);
		expect(store.syncCursorOnClick).toBe(true);
		expect(store.autoScrollOnEdgeCreep).toBe(true);
		expect(store.matRipple).toBe(true);
	});

	it("persists updates to localStorage via property assignments", () => {
		const store = new RenderSettings();
		store.drawPileThickness = "capped";
		expect(localStorage.getItem(SETTINGS_STORAGE_KEY)).toContain('"drawPileThickness":"capped"');
		store.syncCursorOnClick = false;
		expect(localStorage.getItem(SETTINGS_STORAGE_KEY)).toContain('"syncCursorOnClick":false');
		store.cardRenderMode = "instanced";
		expect(localStorage.getItem(SETTINGS_STORAGE_KEY)).toContain('"cardRenderMode":"instanced"');
		store.clickToPlay = false;
		expect(localStorage.getItem(SETTINGS_STORAGE_KEY)).toContain('"clickToPlay":false');
		store.autoScrollOnEdgeCreep = false;
		expect(localStorage.getItem(SETTINGS_STORAGE_KEY)).toContain('"autoScrollOnEdgeCreep":false');
	});

	it("persists updates to localStorage via setter methods", () => {
		const store = new RenderSettings();
		store.setCardRenderMode("legacy");
		store.setDrawPileThickness("capped");
		store.setClickToPlay(false);
		store.setSyncCursorOnClick(false);
		store.setAutoScrollOnEdgeCreep(false);
		store.setMatRipple(false);
		store.setAmbientDust(true);

		const persisted = JSON.parse(localStorage.getItem(SETTINGS_STORAGE_KEY)!);
		expect(persisted).toEqual({
			cardRenderMode: "legacy",
			drawPileThickness: "capped",
			clickToPlay: false,
			syncCursorOnClick: false,
			autoScrollOnEdgeCreep: false,
			matRipple: false,
			ambientDust: true
		});
	});

	it("hydrates from valid localStorage, ignoring removed legacy keys", () => {
		localStorage.setItem(
			SETTINGS_STORAGE_KEY,
			JSON.stringify({
				cardRenderMode: "legacy",
				drawPileThickness: "capped",
				clickToPlay: false,
				syncCursorOnClick: false,
				autoScrollOnEdgeCreep: false,
				matRipple: false,
				ambientDust: true,
				removedSettingKey: "legacy-value"
			})
		);
		const store = new RenderSettings();
		expect(store.cardRenderMode).toBe("legacy");
		expect(store.drawPileThickness).toBe("capped");
		expect(store.clickToPlay).toBe(false);
		expect(store.syncCursorOnClick).toBe(false);
		expect(store.autoScrollOnEdgeCreep).toBe(false);
		expect(store.matRipple).toBe(false);
		expect(store.ambientDust).toBe(true);
	});

	it("handles malformed JSON in localStorage gracefully", () => {
		localStorage.setItem(SETTINGS_STORAGE_KEY, "{corrupt-json");
		const store = new RenderSettings();
		expect(store.cardRenderMode).toBe("atlas");
		expect(["full", "capped"]).toContain(store.drawPileThickness);
		expect(store.clickToPlay).toBe(true);
		expect(store.syncCursorOnClick).toBe(true);
		expect(store.autoScrollOnEdgeCreep).toBe(true);
	});

	it("gracefully handles localStorage.setItem throwing quota exceptions", () => {
		const store = new RenderSettings();
		vi.spyOn(Storage.prototype, "setItem").mockImplementation(() => {
			throw new Error("QuotaExceededError");
		});

		expect(() => {
			store.cardRenderMode = "legacy";
		}).not.toThrow();
		expect(store.cardRenderMode).toBe("legacy");
	});

	it("merges matRipple and ambientDust defaults into a saved blob missing those keys", () => {
		localStorage.setItem(
			SETTINGS_STORAGE_KEY,
			JSON.stringify({
				cardRenderMode: "legacy",
				drawPileThickness: "capped",
				clickToPlay: false,
				syncCursorOnClick: false,
				autoScrollOnEdgeCreep: false
			})
		);
		const store = new RenderSettings();
		expect(store.matRipple).toBe(true);
		expect(typeof store.ambientDust).toBe("boolean");
	});
});

describe("RenderSettings tier defaults", () => {
	const originalHardwareAccelerated = storeWebglCapability.hardwareAccelerated;
	const originalReducedMotion = storeWebglCapability.reducedMotion;

	beforeEach(() => {
		localStorage.clear();
	});

	afterEach(() => {
		storeWebglCapability.hardwareAccelerated = originalHardwareAccelerated;
		storeWebglCapability.reducedMotion = originalReducedMotion;
	});

	it("defaults ambientDust to true on the high device tier", () => {
		storeWebglCapability.hardwareAccelerated = true;
		storeWebglCapability.reducedMotion = false;
		expect(storeWebglCapability.deviceTier).toBe("high");

		const store = new RenderSettings();
		expect(store.ambientDust).toBe(true);
		expect(store.matRipple).toBe(true);
	});

	it("defaults ambientDust to false on the low device tier", () => {
		storeWebglCapability.hardwareAccelerated = false;
		storeWebglCapability.reducedMotion = false;
		expect(storeWebglCapability.deviceTier).toBe("low");

		const store = new RenderSettings();
		expect(store.ambientDust).toBe(false);
		expect(store.matRipple).toBe(true);
	});
});

describe("RenderSettings active getters", () => {
	const originalHardwareAccelerated = storeWebglCapability.hardwareAccelerated;
	const originalReducedMotion = storeWebglCapability.reducedMotion;
	const originalAnimationEnabled = storeAnimation.enabled;

	beforeEach(() => {
		localStorage.clear();
		storeWebglCapability.hardwareAccelerated = true;
		storeWebglCapability.reducedMotion = false;
		storeAnimation.enabled = true;
	});

	afterEach(() => {
		storeWebglCapability.hardwareAccelerated = originalHardwareAccelerated;
		storeWebglCapability.reducedMotion = originalReducedMotion;
		storeAnimation.enabled = originalAnimationEnabled;
	});

	it("both active getters are true when toggles are on and no gate is engaged", () => {
		const store = new RenderSettings();
		store.matRipple = true;
		store.ambientDust = true;
		expect(store.matRippleActive).toBe(true);
		expect(store.ambientDustActive).toBe(true);
	});

	it("both active getters are false under reduced motion while saved values stay true", () => {
		const store = new RenderSettings();
		store.matRipple = true;
		store.ambientDust = true;

		storeWebglCapability.reducedMotion = true;

		expect(store.matRippleActive).toBe(false);
		expect(store.ambientDustActive).toBe(false);
		expect(store.matRipple).toBe(true);
		expect(store.ambientDust).toBe(true);
	});

	it("matRippleActive is false when animations are disabled", () => {
		const store = new RenderSettings();
		store.matRipple = true;
		storeAnimation.enabled = false;

		expect(store.matRippleActive).toBe(false);
		expect(store.matRipple).toBe(true);
	});

	it("ambientDustActive is false when animations are disabled", () => {
		const store = new RenderSettings();
		store.ambientDust = true;
		storeAnimation.enabled = false;

		expect(store.ambientDustActive).toBe(false);
		expect(store.ambientDust).toBe(true);
	});
});
