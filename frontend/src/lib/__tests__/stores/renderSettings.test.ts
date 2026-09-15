import { describe, it, expect, beforeEach, vi } from "vitest";
import {
	RenderSettings,
	SETTINGS_STORAGE_KEY,
	storeRenderSettings
} from "$stores/renderSettings.svelte";

describe("RenderSettings store", () => {
	beforeEach(() => {
		localStorage.clear();
		vi.restoreAllMocks();
	});

	it("initializes with default values", () => {
		const store = new RenderSettings();
		expect(store.cardRenderMode).toBe("atlas");
		expect(["full", "capped"]).toContain(store.drawPileThickness);
		expect(store.handEdgeEffect).toBe("vignette");
		expect(store.syncCursorOnClick).toBe(true);
		expect(store.autoScrollOnEdgeCreep).toBe(true);
	});

	it("persists updates to localStorage via property assignments", () => {
		const store = new RenderSettings();
		store.drawPileThickness = "capped";
		expect(localStorage.getItem(SETTINGS_STORAGE_KEY)).toContain('"drawPileThickness":"capped"');
		store.syncCursorOnClick = false;
		expect(localStorage.getItem(SETTINGS_STORAGE_KEY)).toContain('"syncCursorOnClick":false');
		store.cardRenderMode = "instanced";
		expect(localStorage.getItem(SETTINGS_STORAGE_KEY)).toContain('"cardRenderMode":"instanced"');
		store.handEdgeEffect = "fadeOverlay";
		expect(localStorage.getItem(SETTINGS_STORAGE_KEY)).toContain('"handEdgeEffect":"fadeOverlay"');
		store.autoScrollOnEdgeCreep = false;
		expect(localStorage.getItem(SETTINGS_STORAGE_KEY)).toContain('"autoScrollOnEdgeCreep":false');
	});

	it("persists updates to localStorage via setter methods", () => {
		const store = new RenderSettings();
		store.setCardRenderMode("legacy");
		store.setDrawPileThickness("capped");
		store.setHandEdgeEffect("fadeOverlay");
		store.setSyncCursorOnClick(false);
		store.setAutoScrollOnEdgeCreep(false);

		const persisted = JSON.parse(localStorage.getItem(SETTINGS_STORAGE_KEY)!);
		expect(persisted).toEqual({
			cardRenderMode: "legacy",
			drawPileThickness: "capped",
			handEdgeEffect: "fadeOverlay",
			syncCursorOnClick: false,
			autoScrollOnEdgeCreep: false
		});
	});

	it("hydrates from valid localStorage", () => {
		localStorage.setItem(
			SETTINGS_STORAGE_KEY,
			JSON.stringify({
				cardRenderMode: "legacy",
				drawPileThickness: "capped",
				handEdgeEffect: "fadeOverlay",
				syncCursorOnClick: false,
				autoScrollOnEdgeCreep: false
			})
		);
		const store = new RenderSettings();
		expect(store.cardRenderMode).toBe("legacy");
		expect(store.drawPileThickness).toBe("capped");
		expect(store.handEdgeEffect).toBe("fadeOverlay");
		expect(store.syncCursorOnClick).toBe(false);
		expect(store.autoScrollOnEdgeCreep).toBe(false);
	});

	it("handles malformed JSON in localStorage gracefully", () => {
		localStorage.setItem(SETTINGS_STORAGE_KEY, "{corrupt-json");
		const store = new RenderSettings();
		expect(store.cardRenderMode).toBe("atlas");
		expect(["full", "capped"]).toContain(store.drawPileThickness);
		expect(store.handEdgeEffect).toBe("vignette");
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

	it("exports a storeRenderSettings singleton instance", () => {
		expect(storeRenderSettings).toBeInstanceOf(RenderSettings);
	});
});
