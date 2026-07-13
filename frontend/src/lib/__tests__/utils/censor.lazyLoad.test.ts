import { describe, it, expect } from "vitest";
import { censorText, censorState, loadCensorData } from "$utils/censor.svelte";

// Separate file so the module graph is fresh and loadCensorData() hasn't
// been called by any other test yet, proves the "not loaded" default is
// safe (unmasked, not throwing) before anything triggers the lazy import.
describe("censor lazy loading", () => {
	it("is not ready before anything calls loadCensorData()", () => {
		expect(censorState.ready).toBe(false);
	});

	it("returns text unchanged while not yet loaded", () => {
		expect(censorText("nigga")).toBe("nigga");
	});

	it("becomes ready and starts censoring once loadCensorData() resolves", async () => {
		await loadCensorData();
		expect(censorState.ready).toBe(true);
		expect(censorText("nigga")).toBe("*****");
	});

	it("loadCensorData() is idempotent, calling it again is a cheap no-op", async () => {
		await loadCensorData();
		await loadCensorData();
		expect(censorState.ready).toBe(true);
	});
});
