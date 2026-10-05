import { describe, expect, it } from "vitest";
import { getGlossaryEntry, hasGlossaryKeyword } from "$lib/glossary/glossary";

describe("namespaced glossary", () => {
	it("resolves a namespaced mechanic", () => {
		expect(getGlossaryEntry("uni:turn", "en")?.title).toBeTruthy();
		expect(hasGlossaryKeyword("uni:turn")).toBe(true);
	});

	it("resolves a uni card kind from the card info index", () => {
		const entry = getGlossaryEntry("uni:red_draw2", "en");
		expect(entry?.title).toBeTruthy();
		expect(entry?.description).toContain("[k=uni:draw]");
	});

	it("resolves the uni namespace", () => {
		expect(getGlossaryEntry("uni:turn", "en")?.title).toBeTruthy();
		expect(hasGlossaryKeyword("uni:turn")).toBe(true);
		expect(getGlossaryEntry("uni:red_draw2", "en")?.title).toBeTruthy();
		expect(getGlossaryEntry("uni:nope", "en")).toBeNull();
	});

	it("returns null for an unknown keyword", () => {
		expect(getGlossaryEntry("uni:nope", "en")).toBeNull();
		expect(getGlossaryEntry("nope", "en")).toBeNull();
		expect(hasGlossaryKeyword("uni:nope")).toBe(false);
	});
});
