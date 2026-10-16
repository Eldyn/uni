import { describe, it, expect } from "vitest";
import { get } from "svelte/store";
import {
	director,
	setCleanView,
	togglePlayerHidden,
	setXRay,
	CLEAN_VIEW_BODY_CLASS
} from "$lib/stores/director.svelte";

describe("director store", () => {
	it("starts with clean view off and x-ray on", () => {
		const s = get(director);
		expect(s.cleanView).toBe(false);
		expect(s.xRay).toBe(true);
		expect(s.hidePlayers).toEqual([]);
	});

	it("toggles clean view", () => {
		setCleanView(true);
		expect(get(director).cleanView).toBe(true);
		setCleanView(false);
		expect(get(director).cleanView).toBe(false);
	});

	it("mirrors clean view onto a global body class for chrome hiding", () => {
		setCleanView(true);
		expect(document.body.classList.contains(CLEAN_VIEW_BODY_CLASS)).toBe(true);
		setCleanView(false);
		expect(document.body.classList.contains(CLEAN_VIEW_BODY_CLASS)).toBe(false);
	});

	it("hides and unhides players", () => {
		togglePlayerHidden("bob");
		expect(get(director).hidePlayers).toEqual(["bob"]);
		togglePlayerHidden("bob");
		expect(get(director).hidePlayers).toEqual([]);
	});

	it("switches x-ray", () => {
		setXRay(false);
		expect(get(director).xRay).toBe(false);
		setXRay(true);
		expect(get(director).xRay).toBe(true);
	});
});
