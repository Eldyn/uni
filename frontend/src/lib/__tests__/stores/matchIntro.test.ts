import { describe, it, expect, beforeEach } from "vitest";
import { storeMatchIntro } from "$lib/stores/matchIntro.svelte";

describe("storeMatchIntro", () => {
	beforeEach(() => {
		storeMatchIntro.end();
	});

	it("starts neutral", () => {
		expect(storeMatchIntro.active).toBe(false);
		expect(storeMatchIntro.drawPileCount).toBeNull();
		expect(storeMatchIntro.drawPilePos).toBeNull();
		expect(storeMatchIntro.discardHidden).toBe(false);
		expect(storeMatchIntro.forcePurpleMat).toBe(false);
	});

	it("begin() sets the three flags and leaves the pile overrides null", () => {
		storeMatchIntro.begin();

		expect(storeMatchIntro.active).toBe(true);
		expect(storeMatchIntro.discardHidden).toBe(true);
		expect(storeMatchIntro.forcePurpleMat).toBe(true);
		expect(storeMatchIntro.drawPileCount).toBeNull();
		expect(storeMatchIntro.drawPilePos).toBeNull();
	});

	it("end() resets every field to neutral from a mid-cinematic state", () => {
		storeMatchIntro.begin();
		storeMatchIntro.drawPileCount = 3;
		storeMatchIntro.drawPilePos = { x: 1.5, z: -2 };

		storeMatchIntro.end();

		expect(storeMatchIntro.active).toBe(false);
		expect(storeMatchIntro.drawPileCount).toBeNull();
		expect(storeMatchIntro.drawPilePos).toBeNull();
		expect(storeMatchIntro.discardHidden).toBe(false);
		expect(storeMatchIntro.forcePurpleMat).toBe(false);
	});
});
